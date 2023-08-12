/*
 * Copy me if you can.
 * by 20h
 */

#include <limits.h>
#include <unistd.h>
#include <dirent.h>
#include <memory.h>
#include <netdb.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <syslog.h>

#ifdef ENABLE_TLS
#include <tls.h>
#endif /* ENABLE_TLS */

#include "ind.h"
#include "handlr.h"
#include "arg.h"

enum {
	NOLOG	= 0,
	FILES	= 1,
	DIRS	= 2,
	HTTP	= 4,
	ERRORS	= 8,
	CONN	= 16,
	GPLUS	= 32
};

int glfd = -1;
int dosyslog = 0;
int logpriority = LOG_INFO|LOG_DAEMON;
int loglvl = 47;
int revlookup = 1;
char *logfile = NULL;

int *listfds = NULL;
int nlistfds = 0;

char *argv0;
char stdbase[] = "/var/gopher";
char *stdport = "70";
char *indexf[] = {"index.gph", "index.cgi", "index.dcgi", "index.bob", "index.bin"};
char *nocgierr = "3Sorry, execution of the token '%s' was requested, but this "
	    "is disabled in the server configuration.\tErr"
	    "\tlocalhost\t70\r\n";
char *notfounderr = "3Sorry, but the requested token '%s' could not be found.\tErr"
	    "\tlocalhost\t70\r\n";
char *toolongerr = "3Sorry, but the requested token '%s' is a too long path.\tErr"
	    "\tlocalhost\t70\r\n";
char *tlserr = "3Sorry, but the requested token '%s' requires an encrypted connection.\tErr"
	    "\tlocalhost\t70\r\n";
char *htredir = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\"\n"
		"	\"DTD/xhtml-transitional.dtd\">\n"
		"<html xmlns=\"http://www.w3.org/1999/xhtml\" lang=\"en\">\n"
		"  <head>\n"
		"    <title>gopher redirect</title>\n"
		"\n"
		"    <meta http-equiv=\"Refresh\" content=\"1;url=%s\" />\n"
		"  </head>\n"
		"  <body>\n"
		"    This page is for redirecting you to: <a href=\"%s\">%s</a>.\n"
		"  </body>\n"
		"</html>\n";
char *selinval ="3Happy helping ☃ here: "
		"Sorry, your selector does contains '..'. "
		"That's illegal here.\tErr\tlocalhost\t70\r\n.\r\n\r\n";

int
dropprivileges(struct group *gr, struct passwd *pw)
{
	if (gr != NULL)
		if (setgroups(1, &gr->gr_gid) != 0 || setgid(gr->gr_gid) != 0)
			return -1;
	if (pw != NULL) {
		if (gr == NULL) {
			if (setgroups(1, &pw->pw_gid) != 0 ||
			    setgid(pw->pw_gid) != 0)
				return -1;
		}
		if (setuid(pw->pw_uid) != 0)
			return -1;
	}

	return 0;
}

void
logentry(char *host, char *port, char *qry, char *status)
{
	time_t tim;
	struct tm *ptr;
	char timstr[128], *ahost;

        if (glfd >= 0 || dosyslog) {
		ahost = revlookup ? reverselookup(host) : host;
		if (dosyslog) {
			syslog(logpriority, "[%s|%s|%s] %s\n", ahost, port,
					status, qry);
		} else {
			tim = time(0);
			ptr = gmtime(&tim);
			strftime(timstr, sizeof(timstr), "%F %T %z", ptr);
			dprintf(glfd, "[%s|%s|%s|%s] %s\n",
				timstr, ahost, port, status, qry);
		}
		if (revlookup)
			free(ahost);
        }

	return;
}

void
handlerequest(int sock, char *req, int rlen, char *base, char *ohost,
	      char *port, char *clienth, char *clientp, char *serverh,
	      char *serverp, int nocgi, int istls)
{
	struct stat dir;
	char recvc[1025], recvb[1025], path[PATH_MAX+1], rpath[PATH_MAX+1], args[1025],
		argsc[1025], traverse[1025], traversec[1025],
		*sear, *sep, *recvbp, *c;
	int len = 0, fd, i, maxrecv, pathfallthrough = 0;
	filetype *type;

	if (!istls) {
		/*
		 * If sticky bit is set on base dir and encryption is not
		 * used, do not serve.
		 */
		if (stat(*base? base : "/", &dir) == -1)
			return;
		if (dir.st_mode & S_ISVTX) {
			dprintf(sock, tlserr, recvc);
			if (loglvl & ERRORS) {
				logentry(clienth, clientp, recvc,
					"encryption only");
			}
			return;
		}
	}

	memset(&dir, 0, sizeof(dir));
	memset(recvb, 0, sizeof(recvb));
	memset(recvc, 0, sizeof(recvc));
	memset(args, 0, sizeof(args));
	memset(argsc, 0, sizeof(argsc));
	memset(traverse, 0, sizeof(argsc));

	maxrecv = sizeof(recvb) - 1;
	if (rlen > maxrecv || rlen < 0)
		return;
	memcpy(recvb, req, rlen);

	c = strchr(recvb, '\r');
	if (c)
		c[0] = '\0';
	c = strchr(recvb, '\n');
	if (c)
		c[0] = '\0';

	memmove(recvc, recvb, rlen+1);
	/*
	 * Try to guess if we have some HTTP-like protocol compatibility
	 * mode.
	 */
	if (!nocgi && recvb[0] != '/' && (c = strchr(recvb, ' '))) {
		*c++ = '\0';
		if (strchr(recvb, '/'))
			goto dothegopher;
		if (snprintf(path, sizeof(path), "%s/%s", base, recvb) <= sizeof(path)) {
			if (realpath(path, (char *)rpath)) {
				if (stat(rpath, &dir) == 0) {
					if (loglvl & FILES)
						logentry(clienth, clientp, recvc, "compatibility serving");

					handlecgi(sock, rpath, port, base, "", "", ohost,
						clienth, serverh, istls, req, "");
					return;
				}
			}
		}
	}
dothegopher:

	/* Do not allow requests including "..". */
	if (strstr(recvb, "..")) {
		dprintf(sock, "%s", selinval);
		return;
	}

	sear = strchr(recvb, '\t');
	if (sear != NULL) {
		*sear++ = '\0';

		/*
		 * This is a compatibility layer to geomyidae for users using
		 * the original gopher(1) client. Gopher+ is by default
		 * requesting the metadata. We are using a trick in the
		 * gopher(1) parsing code to jump back to gopher compatibility
		 * mode. DO NOT ADD ANY OTHER GOPHER+ SUPPORT. GOPHER+ IS
		 * CRAP.
		 */
		if ((sear[0] == '+' && sear[1] == '\0')
				|| (sear[0] == '$' && sear[1] == '\0')
				|| (sear[0] == '!' && sear[1] == '\0')
				|| sear[0] == '\0') {
			if (loglvl & GPLUS)
				logentry(clienth, clientp, recvb, "gopher+ redirect");
			dprintf(sock, "+-2\r\n");
			dprintf(sock, "+INFO: 1gopher+\t\t%s\t%s\r\n",
					ohost, port);
			dprintf(sock, "+ADMIN:\r\n Admin: Me\r\n");
			return;
		}
	}

	memmove(recvc, recvb, rlen+1);

	/* Redirect to HTML redirecting to the specified URI. */
	if (!strncmp(recvb, "URL:", 4)) {
		len = snprintf(path, sizeof(path), htredir,
				recvb + 4, recvb + 4, recvb + 4);
		if (len > sizeof(path))
			len = sizeof(path);
		write(sock, path, len);
		if (loglvl & HTTP)
			logentry(clienth, clientp, recvc, "HTTP redirect");
		return;
	}

	/* Strip off the arguments of req?args style. */
	c = strchr(recvb, '?');
	if (c != NULL) {
		*c++ = '\0';
		snprintf(args, sizeof(args), "%s", c);
	}

	/* Strip '/' at the end of the request. */
	for (c = recvb + strlen(recvb) - 1; c >= recvb && c[0] == '/'; c--) {
		memmove(traversec, traverse, strlen(traverse));
		/* Prepend to traverse. */
		snprintf(traverse, sizeof(traverse), "/%s", traversec);
		c[0] = '\0';
	}

	/* path is now always at least '/' */
	if (snprintf(path, sizeof(path), "%s%s%s", base,
	    (*recvb != '/')? "/" : "",
	    recvb) > sizeof(path)) {
		if (loglvl & ERRORS) {
			logentry(clienth, clientp, recvc,
				"path truncation occurred");
		}
		dprintf(sock, toolongerr, recvc);
		return;
	}

	fd = -1;
	/*
	 * If path could not be found, do:
	 * 1.) Traverse from base directory one dir by dir.
	 * 2.) If one path element, separated by "/", is not found, stop.
	 * 3.) Prepare new args string:
	 *
	 *	$args = $rest_of_path + "?" + $args
	 */
	if (stat(path, &dir) == -1) {
		memmove(traversec, traverse, strlen(traverse));
		snprintf(path, sizeof(path), "%s", base);
		recvbp = recvb;

		/*
		 * Walk into the selector until some directory or file
		 * does not exist. Then reconstruct the args, selector
		 * etc.
		 */
		while (recvbp != NULL) {
			/* Traverse multiple empty / in selector. */
			while(recvbp[0] == '/')
				recvbp++;
			sep = strchr(recvbp, '/');
			if (sep != NULL)
				*sep++ = '\0';

			snprintf(path+strlen(path), sizeof(path)-strlen(path),
				"/%s", recvbp);
			/* path is now always at least '/' */
			if (stat(path, &dir) == -1) {
				path[strlen(path)-strlen(recvbp)-1] = '\0';
				snprintf(traverse, sizeof(traverse),
					"/%s%s%s%s",
					recvbp,
					(sep != NULL)? "/" : "",
					(sep != NULL)? sep : "",
					(traversec[0] != '\0')? traversec : ""
				);
				/* path fallthrough */
				pathfallthrough = 1;
				break;
			}
			/* Append found directory to path. */
			recvbp = sep;
		}
	}

	if (realpath(path, (char *)&rpath) == NULL) {
		dprintf(sock, notfounderr, recvc);
		if (loglvl & ERRORS)
			logentry(clienth, clientp, recvc, "not found");
	}
	if (stat(rpath, &dir) != -1) {
		/*
		 * If sticky bit is set, only serve if this is encrypted.
		 */
		if ((dir.st_mode & S_ISVTX) && !istls) {
			dprintf(sock, tlserr, recvc);
			if (loglvl & ERRORS) {
				logentry(clienth, clientp, recvc,
					"encryption only");
			}
			return;
		}

		if (S_ISDIR(dir.st_mode)) {
			for (i = 0; i < sizeof(indexf)/sizeof(indexf[0]);
					i++) {
				len = strlen(rpath);
				if (len + strlen(indexf[i]) + ((rpath[len-1] == '/')? 0 : 1)
						>= sizeof(rpath)) {
					if (loglvl & ERRORS) {
						logentry(clienth, clientp,
							recvc,
							"path truncation occurred");
					}
					return;
				}
				/*
				 * The size check for strcat to work is
				 * above.
				 *
				 * Until strlcat isn't properly in all
				 * linux libcs, we keep to this. OpenBSD
				 * will complain about strcat and
				 * smart-ass gcc will cmplain about
				 * strncat of one char static char array
				 * is an overflow.
				 */
				if (rpath[len-1] != '/')
					strcat(rpath, "/");
				strcat(rpath, indexf[i]);
				fd = open(rpath, O_RDONLY);
				if (fd >= 0)
					break;

				/* Not found. Clear path from indexf. */
				rpath[len] = '\0';
			}
		} else {
			fd = open(rpath, O_RDONLY);
			if (fd < 0) {
				dprintf(sock, notfounderr, recvc);
				if (loglvl & ERRORS) {
					logentry(clienth, clientp, recvc,
						strerror(errno));
				}
				return;
			}
		}
	}

	/* Some file was opened. Serve it. */
	if (fd >= 0) {
		close(fd);

		c = strrchr(rpath, '/');
		if (c == NULL)
			c = rpath;
		type = gettype(c);

		/*
		 * If we had to traverse the path to find some, only
		 * allow index.dcgi and index.cgi as handlers.
		 */
		if (pathfallthrough &&
				!(type->f == handledcgi || type->f == handlecgi)) {
			dprintf(sock, notfounderr, recvc);
			if (loglvl & ERRORS) {
				logentry(clienth, clientp, recvc,
					"handler in path fallthrough not allowed");
			}
			return;
		}

		if (nocgi && (type->f == handledcgi || type->f == handlecgi)) {
			dprintf(sock, nocgierr, recvc);
			if (loglvl & ERRORS)
				logentry(clienth, clientp, recvc, "nocgi error");
		} else {
			if (loglvl & FILES)
				logentry(clienth, clientp, recvc, "serving");

			type->f(sock, rpath, port, base, args, sear, ohost,
				clienth, serverh, istls, recvc, traverse);
		}
	} else {
		if (pathfallthrough && S_ISDIR(dir.st_mode)) {
			if (loglvl & ERRORS) {
				logentry(clienth, clientp, recvc,
					"directory listing in traversal not allowed");
			}
			return;
		}

		if (!pathfallthrough && S_ISDIR(dir.st_mode)) {
			handledir(sock, rpath, port, base, args, sear, ohost,
				clienth, serverh, istls, recvc, traverse);
			if (loglvl & DIRS) {
				logentry(clienth, clientp, recvc,
							"dir listing");
			}
			return;
		}

		dprintf(sock, notfounderr, recvc);
		if (loglvl & ERRORS)
			logentry(clienth, clientp, recvc, "not found");
	}

	return;
}

void
sighandler(int sig)
{
	int i;

	switch (sig) {
	case SIGCHLD:
		while (waitpid(-1, NULL, WNOHANG) > 0);
		break;
	case SIGINT:
	case SIGQUIT:
	case SIGABRT:
	case SIGTERM:
	case SIGKILL:
		if (dosyslog) {
			closelog();
		} else if (logfile != NULL && glfd != -1) {
			close(glfd);
			glfd = -1;
		}

		for (i = 0; i < nlistfds; i++) {
			shutdown(listfds[i], SHUT_RDWR);
			close(listfds[i]);
		}
		free(listfds);
		exit(0);
		break;
	default:
		break;
	}
}

void
initsignals(void)
{
	signal(SIGCHLD, sighandler);
	signal(SIGHUP, sighandler);
	signal(SIGINT, sighandler);
	signal(SIGQUIT, sighandler);
	signal(SIGABRT, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGKILL, sighandler);

	signal(SIGPIPE, SIG_IGN);
}

/*
 * TODO: Move Linux and BSD to Plan 9 socket and bind handling, so we do not
 *       need the inconsistent return and exit on getaddrinfo.
 */
int *
getlistenfd(struct addrinfo *hints, char *bindip, char *port, int *rlfdnum)
{
	char addstr[INET6_ADDRSTRLEN];
	struct addrinfo *ai, *rp;
	void *sinaddr;
	int on, *listenfds, *listenfd, aierr, errno_save;

	if ((aierr = getaddrinfo(bindip, port, hints, &ai)) || ai == NULL) {
		fprintf(stderr, "getaddrinfo (%s:%s): %s\n", bindip, port,
			gai_strerror(aierr));
		exit(1);
	}

	*rlfdnum = 0;
	listenfds = NULL;
	on = 1;
	for (rp = ai; rp != NULL; rp = rp->ai_next) {
		listenfds = xrealloc(listenfds,
				sizeof(*listenfds) * (++*rlfdnum));
		listenfd = &listenfds[*rlfdnum-1];

		*listenfd = socket(rp->ai_family, rp->ai_socktype,
				rp->ai_protocol);
		if (*listenfd < 0)
			continue;
		if (setsockopt(*listenfd, SOL_SOCKET, SO_REUSEADDR, &on,
				sizeof(on)) < 0) {
			close(*listenfd);
			(*rlfdnum)--;
			continue;
		}

		if (rp->ai_family == AF_INET6 && (setsockopt(*listenfd,
				IPPROTO_IPV6, IPV6_V6ONLY, &on,
				sizeof(on)) < 0)) {
			close(*listenfd);
			(*rlfdnum)--;
			continue;
		}

		sinaddr = (rp->ai_family == AF_INET) ?
		          (void *)&((struct sockaddr_in *)rp->ai_addr)->sin_addr :
		          (void *)&((struct sockaddr_in6 *)rp->ai_addr)->sin6_addr;

		if (bind(*listenfd, rp->ai_addr, rp->ai_addrlen) == 0) {
			if (loglvl & CONN && inet_ntop(rp->ai_family, sinaddr,
					addstr, sizeof(addstr))) {
				/* Do not revlookup here. */
				on = revlookup;
				revlookup = 0;
				logentry(addstr, port, "-", "listening");
				revlookup = on;
			}
			continue;
		}

		/* Save errno, because fprintf in logentry overwrites it. */
		errno_save = errno;
		close(*listenfd);
		(*rlfdnum)--;
		if (loglvl & CONN && inet_ntop(rp->ai_family, sinaddr,
				addstr, sizeof(addstr))) {
			/* Do not revlookup here. */
			on = revlookup;
			revlookup = 0;
			logentry(addstr, port, "-", "could not bind");
			revlookup = on;
		}
		errno = errno_save;
	}
	freeaddrinfo(ai);
	if (*rlfdnum < 1) {
		free(listenfds);
		return NULL;
	}

	return listenfds;
}

void
usage(void)
{
	dprintf(2, "usage: %s [-46cdensy] [-l logfile] "
#ifdef ENABLE_TLS
		   "[-t keyfile certfile] "
#endif /* ENABLE_TLS */
	           "[-v loglvl] [-b base] [-p port] [-o sport] "
	           "[-u user] [-g group] [-h host] [-i interface ...]\n",
		   argv0);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct addrinfo hints;
	struct sockaddr_storage clt, slt;
	socklen_t cltlen, sltlen;
	int sock, dofork = 1, inetf = AF_UNSPEC, usechroot = 0,
	    nocgi = 0, errno_save, nbindips = 0, i, j,
	    nlfdret, *lfdret, listfd, maxlfd, istls = 0,
	    dotls = 0, dohaproxy = 0, tcpver = -1, haret = 0,
#ifdef ENABLE_TLS
	    tlssocks[2], shufbuf[1025],
	    shuflen, wlen, shufpos, tlsclientreader,
#endif /* ENABLE_TLS */
	    maxrecv, retl,
	    rlen = 0;
	fd_set rfd;
	char *port, *base, clienth[NI_MAXHOST], clientp[NI_MAXSERV],
	     *user = NULL, *group = NULL, **bindips = NULL,
	     *ohost = NULL, *sport = NULL, *p;
	/* Must be as large as recvb, due to scanf restrictions. */
	char hachost[1025], hashost[1025], hacport[1025], hasport[1025],
#ifdef ENABLE_TLS
	     *certfile = NULL, *keyfile = NULL,
#endif /* ENABLE_TLS */
	     byte0, recvb[1025], serverh[NI_MAXHOST], serverp[NI_MAXSERV];
	struct passwd *us = NULL;
	struct group *gr = NULL;
#ifdef ENABLE_TLS
	struct tls_config *tlsconfig = NULL;
	struct tls *tlsctx = NULL, *tlsclientctx;
#endif /* ENABLE_TLS */

	base = stdbase;
	port = stdport;

	ARGBEGIN {
	case '4':
		inetf = AF_INET;
		tcpver = 4;
		break;
	case '6':
		inetf = AF_INET6;
		tcpver = 6;
		break;
	case 'b':
		base = EARGF(usage());
		break;
	case 'c':
		usechroot = 1;
		break;
	case 'd':
		dofork = 0;
		break;
	case 'e':
		nocgi = 1;
		break;
	case 'g':
		group = EARGF(usage());
		break;
	case 'h':
		ohost = EARGF(usage());
		break;
	case 'i':
		bindips = xrealloc(bindips, sizeof(*bindips) * (++nbindips));
		bindips[nbindips-1] = EARGF(usage());
		break;
	case 'l':
		logfile = EARGF(usage());
		break;
	case 'n':
		revlookup = 0;
		break;
	case 'o':
		sport = EARGF(usage());
		break;
	case 'p':
		port = EARGF(usage());
		if (sport == NULL)
			sport = port;
		break;
	case 's':
		dosyslog = 1;
		break;
#ifdef ENABLE_TLS
	case 't':
		dotls = 1;
		keyfile = EARGF(usage());
		certfile = EARGF(usage());
		break;
#endif /* ENABLE_TLS */
	case 'u':
		user = EARGF(usage());
		break;
	case 'v':
		loglvl = atoi(EARGF(usage()));
		break;
	case 'y':
		dohaproxy = 1;
		break;
	default:
		usage();
	} ARGEND;

	if (sport == NULL)
		sport = port;

	if (argc != 0)
		usage();

#ifdef ENABLE_TLS
	if (dotls) {
		if (tls_init() < 0) {
			perror("tls_init");
			return 1;
		}
		if ((tlsconfig = tls_config_new()) == NULL) {
			perror("tls_config_new");
			return 1;
		}
		if ((tlsctx = tls_server()) == NULL) {
			perror("tls_server");
			return 1;
		}
		if (tls_config_set_key_file(tlsconfig, keyfile) < 0) {
			perror("tls_config_set_key_file");
			return 1;
		}
		if (tls_config_set_cert_file(tlsconfig, certfile) < 0) {
			perror("tls_config_set_cert_file");
			return 1;
		}
		if (tls_configure(tlsctx, tlsconfig) < 0) {
			perror("tls_configure");
			return 1;
		}
	}
#endif /* ENABLE_TLS */

	if (ohost == NULL) {
		/* Do not use HOST_NAME_MAX, it is not defined on NetBSD. */
		ohost = xcalloc(1, 256+1);
		if (gethostname(ohost, 256) < 0) {
			perror("gethostname");
			free(ohost);
			return 1;
		}
	} else {
		ohost = xstrdup(ohost);
	}

	if (group != NULL) {
		errno = 0;
		if ((gr = getgrnam(group)) == NULL) {
			if (errno == 0) {
				fprintf(stderr, "no such group '%s'\n", group);
			} else {
				perror("getgrnam");
			}
			return 1;
		}
	}

	if (user != NULL) {
		errno = 0;
		if ((us = getpwnam(user)) == NULL) {
			if (errno == 0) {
				fprintf(stderr, "no such user '%s'\n", user);
			} else {
				perror("getpwnam");
			}
			return 1;
		}
	}

	if (dofork) {
		switch (fork()) {
		case -1:
			perror("fork");
			return 1;
		case 0:
			break;
		default:
			return 0;
		}
	}

	if (dosyslog) {
		openlog("geomyidae", dofork? LOG_NDELAY|LOG_PID \
				: LOG_CONS|LOG_PERROR, logpriority);
	} else if (logfile != NULL) {
		glfd = open(logfile, O_APPEND | O_WRONLY | O_CREAT, 0644);
		if (glfd < 0) {
			perror("log");
			return 1;
		}
	} else if (!dofork) {
		glfd = 1;
	}

	if (bindips == NULL) {
		if (inetf == AF_INET || inetf == AF_UNSPEC) {
			bindips = xrealloc(bindips, sizeof(*bindips) * (++nbindips));
			bindips[nbindips-1] = "0.0.0.0";
		}
		if (inetf == AF_INET6 || inetf == AF_UNSPEC) {
			bindips = xrealloc(bindips, sizeof(*bindips) * (++nbindips));
			bindips[nbindips-1] = "::";
		}
	}

	for (i = 0; i < nbindips; i++) {
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = inetf;
		hints.ai_flags = AI_PASSIVE;
		hints.ai_socktype = SOCK_STREAM;
		if (bindips[i])
			hints.ai_flags |= AI_CANONNAME;

		nlfdret = 0;
		lfdret = getlistenfd(&hints, bindips[i], port, &nlfdret);
		if (nlfdret < 1) {
			errno_save = errno;
			fprintf(stderr, "Unable to get a binding socket for "
					"%s:%s\n", bindips[i], port);
			errno = errno_save;
			perror("getlistenfd");
		}

		for (j = 0; j < nlfdret; j++) {
			if (listen(lfdret[j], 255) < 0) {
				perror("listen");
				close(lfdret[j]);
				continue;
			}
			listfds = xrealloc(listfds,
					sizeof(*listfds) * ++nlistfds);
			listfds[nlistfds-1] = lfdret[j];
		}
		free(lfdret);
	}
	free(bindips);

	if (nlistfds < 1)
		return 1;

	if (usechroot) {
		if (chdir(base) < 0) {
			perror("chdir");
			return 1;
		}
		base = "";
		if (chroot(".") < 0) {
			perror("chroot");
			return 1;
		}
	} else if (*base != '/' && !(base = realpath(base, NULL))) {
		perror("realpath");
		return 1;
	}

	/* strip / at the end of base */
	for (p = base + strlen(base) - 1; p >= base && p[0] == '/'; --p)
		p[0] = '\0';

	if (dropprivileges(gr, us) < 0) {
		perror("dropprivileges");

		for (i = 0; i < nlistfds; i++) {
			shutdown(listfds[i], SHUT_RDWR);
			close(listfds[i]);
		}
		free(listfds);
		return 1;
	}

	initsignals();

#ifdef HOT_COMPUTER
#warning "I love you too."
#endif

#ifdef __OpenBSD__
	char promises[31]; /* check the size needed in the fork too */
	snprintf(promises, sizeof(promises), "rpath inet stdio proc exec %s",
	         revlookup ? "dns" : "");
	if (pledge(promises, NULL) == -1) {
		perror("pledge");
		exit(1);
	}
#endif /* __OpenBSD__ */

	while (1) {
		FD_ZERO(&rfd);
		maxlfd = 0;
		for (i = 0; i < nlistfds; i++) {
			FD_SET(listfds[i], &rfd);
			if (listfds[i] > maxlfd)
				maxlfd = listfds[i];
		}

		if (pselect(maxlfd+1, &rfd, NULL, NULL, NULL, NULL) < 0) {
			if (errno == EINTR)
				continue;
			perror("pselect");
			break;
		}

		listfd = -1;
		for (i = 0; i < nlistfds; i++) {
			if (FD_ISSET(listfds[i], &rfd)) {
				listfd = listfds[i];
				break;
			}
		}
		if (listfd < 0)
			continue;

		cltlen = sizeof(clt);
		sock = accept(listfd, (struct sockaddr *)&clt, &cltlen);
		if (sock < 0) {
			switch (errno) {
			case ECONNABORTED:
			case EINTR:
				continue;
			default:
				perror("accept");
				close(listfd);
				return 1;
			}
		}

		sltlen = sizeof(slt);
		serverh[0] = serverp[0] = '\0';
		if (getsockname(sock, (struct sockaddr *)&slt, &sltlen) == 0) {
			getnameinfo((struct sockaddr *)&slt, sltlen, serverh,
					sizeof(serverh), serverp, sizeof(serverp),
					NI_NUMERICHOST|NI_NUMERICSERV);
		}
		if (!strncmp(serverh, "::ffff:", 7))
			memmove(serverh, serverh+7, strlen(serverh)-6);

		if (getnameinfo((struct sockaddr *)&clt, cltlen, clienth,
				sizeof(clienth), clientp, sizeof(clientp),
				NI_NUMERICHOST|NI_NUMERICSERV)) {
			clienth[0] = clientp[0] = '\0';
		}

		if (!strncmp(clienth, "::ffff:", 7))
			memmove(clienth, clienth+7, strlen(clienth)-6);

		if (loglvl & CONN)
			logentry(clienth, clientp, "-", "connected");

		switch (fork()) {
		case -1:
			perror("fork");
			shutdown(sock, SHUT_RDWR);
			break;
		case 0:
			close(listfd);

			signal(SIGHUP, SIG_DFL);
			signal(SIGQUIT, SIG_DFL);
			signal(SIGINT, SIG_DFL);
			signal(SIGTERM, SIG_DFL);
			signal(SIGALRM, SIG_DFL);

#ifdef __OpenBSD__
			snprintf(promises, sizeof(promises),
			         "rpath inet stdio %s %s",
			         nocgi     ? ""    : "proc exec",
			         revlookup ? "dns" : "");
			if (pledge(promises, NULL) == -1) {
				perror("pledge");
				exit(1);
			}
#endif /* __OpenBSD__ */

read_selector_again:
			rlen = 0;
			memset(recvb, 0, sizeof(recvb));

			if (recv(sock, &byte0, 1, MSG_PEEK) < 1)
				return 1;

#ifdef ENABLE_TLS
			/*
			 * First byte is 0x16 == 22, which is the TLS
			 * Handshake first byte.
			 */
			istls = 0;
			if (byte0 == 0x16 && dotls) {
				istls = 1;
				if (tls_accept_socket(tlsctx, &tlsclientctx, sock) < 0)
					return 1;
				wlen = TLS_WANT_POLLIN;
				while (wlen == TLS_WANT_POLLIN \
						|| wlen == TLS_WANT_POLLOUT) {
					wlen = tls_handshake(tlsclientctx);
				}
				if (wlen == -1)
					return 1;
			}
#endif /* ENABLE_TLS */
			/*
			 * Some TLS request. Help them determine we only
			 * serve plaintext.
			 */
			if (byte0 == 0x16 && !dotls) {
				if (loglvl & CONN) {
					logentry(clienth, clientp, "-",
							"disconnected");
				}

				shutdown(sock, SHUT_RDWR);
				close(sock);

				return 1;
			}

			maxrecv = sizeof(recvb) - 1;
			do {
#ifdef ENABLE_TLS
				if (istls) {
					retl = tls_read(tlsclientctx,
						recvb+rlen, 1);
					if (retl < 0)
						fprintf(stderr, "tls_read failed: %s\n", tls_error(tlsclientctx));
				} else
#endif /* ENABLE_TLS */
				{
					retl = read(sock, recvb+rlen,
						1);
					if (retl < 0)
						perror("read");
				}
				if (retl <= 0)
					break;
				rlen += retl;
			} while (recvb[rlen-1] != '\n'
					&& --maxrecv > 0);
			if (rlen <= 0)
				return 1;

			/*
			 * HAProxy v1 protocol support.
			 * TODO: Add other protocol version support.
			 */
			if (dohaproxy && !strncmp(recvb, "PROXY TCP", 9)) {
				if (p[-1] == '\r')
					p[-1] = '\0';
				*p++ = '\0';

				/*
				 * Be careful, we are using scanf.
				 * TODO: Use some better parsing.
				 */
				memset(hachost, 0, sizeof(hachost));
				memset(hashost, 0, sizeof(hashost));
				memset(hacport, 0, sizeof(hacport));
				memset(hasport, 0, sizeof(hasport));

				haret = sscanf(recvb, "PROXY TCP%d %s %s %s %s",
					&tcpver, hachost, hashost, hacport,
					hasport);
				if (haret != 5)
					return 1;

				/*
				 * Be careful. Everything could be
				 * malicious.
				 */
				memset(clienth, 0, sizeof(clienth));
				memmove(clienth, hachost, sizeof(clienth)-1);
				memset(serverh, 0, sizeof(serverh));
				memmove(serverh, hashost, sizeof(serverh)-1);
				memset(clientp, 0, sizeof(clientp));
				memmove(clientp, hacport, sizeof(clientp)-1);
				memset(serverp, 0, sizeof(serverp));
				memmove(serverp, hasport, sizeof(serverp)-1);

				if (!strncmp(serverh, "::ffff:", 7)) {
					memmove(serverh, serverh+7,
							strlen(serverh)-6);
				}
				if (!strncmp(clienth, "::ffff:", 7)) {
					memmove(clienth, clienth+7,
							strlen(clienth)-6);
				}
				if (loglvl & CONN) {
					logentry(clienth, clientp, "-",
							"haproxy connection");
				}

				goto read_selector_again;
			}

#ifdef ENABLE_TLS
			if (istls) {
				if (socketpair(AF_LOCAL, SOCK_STREAM, 0, tlssocks) < 0) {
					perror("tls_socketpair");
					return 1;
				}

				switch(fork()) {
				case 0:
					sock = tlssocks[1];
					close(tlssocks[0]);
					break;
				case -1:
					perror("fork");
					return 1;
				default:
					tlsclientreader = 1;
					switch(fork()) {
					case 0:
						break;
					case -1:
						perror("fork");
						return 1;
					default:
						tlsclientreader = 0;
					}

					close(tlssocks[tlsclientreader? 1 : 0]);
					do {
						if (tlsclientreader) {
							shuflen = read(tlssocks[0],
								shufbuf,
								sizeof(shufbuf)-1);
						} else {
							shuflen = tls_read(tlsclientctx,
								shufbuf,
								sizeof(shufbuf)-1);
						}
						if (shuflen == TLS_WANT_POLLIN \
								|| shuflen == TLS_WANT_POLLOUT) {
							continue;
						}
						if (shuflen == -1 && errno == EINTR)
							continue;
						for (shufpos = 0; shufpos < shuflen;
								shufpos += wlen) {
							if (tlsclientreader) {
								wlen = tls_write(tlsclientctx,
									shufbuf+shufpos,
									shuflen-shufpos);
								if (wlen == TLS_WANT_POLLIN
									|| wlen == TLS_WANT_POLLOUT) {
									wlen = 0;
									continue;
								}
								if (wlen < 0) {
									fprintf(stderr,
										"tls_write failed: %s\n",
										tls_error(tlsclientctx));
									return 1;
								}
							} else {
								wlen = write(tlssocks[1],
									shufbuf+shufpos,
									shuflen-shufpos);
								if (wlen < 0) {
									perror("write");
									return 1;
								}
							}
						}
					} while (shuflen > 0);

					if (tlsclientreader) {
						wlen = TLS_WANT_POLLIN;
						while (wlen == TLS_WANT_POLLIN \
								|| wlen == TLS_WANT_POLLOUT) {
							wlen = tls_close(tlsclientctx);
						}
						tls_free(tlsclientctx);
					}

					lingersock(tlssocks[tlsclientreader? 0 : 1]);
					close(tlssocks[tlsclientreader? 0 : 1]);

					if (tlsclientreader) {
						lingersock(sock);
						close(sock);
					}
					return 0;
				}
			}
#endif /* ENABLE_TLS */

			handlerequest(sock, recvb, rlen, base,
					(dohaproxy)? serverh : ohost,
					(dohaproxy)? serverp : sport,
					clienth, clientp, serverh, serverp,
					nocgi, istls);

			lingersock(sock);
			close(sock);

			if (loglvl & CONN) {
				logentry(clienth, clientp, "-",
						"disconnected");
			}

			return 0;
		default:
			break;
		}
		close(sock);
	}

	if (dosyslog) {
		closelog();
	} else if (logfile != NULL && glfd != -1) {
		close(glfd);
		glfd = -1;
	}
	free(ohost);

	for (i = 0; i < nlistfds; i++) {
		shutdown(listfds[i], SHUT_RDWR);
		close(listfds[i]);
	}
	free(listfds);

#ifdef ENABLE_TLS
	if (dotls) {
		tls_close(tlsctx);
		tls_free(tlsctx);
		tls_config_free(tlsconfig);
	}
#endif /* ENABLE_TLS */

	return 0;
}

