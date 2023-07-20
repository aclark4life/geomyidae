/*
 * Copy me if you can.
 * by 20h
 */

#ifdef __linux__
	#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <memory.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <errno.h>

#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define BLOCK_SIZE ((PAGE_SIZE * 16) - 1)

#include "arg.h"
#include "ind.h"
#include "handlr.h"

/*
 * Be careful, to look at handlerequest(), in case you add any executing
 * handler, so nocgi will be valuable.
 *
 * All files are handled as binary, without a following ".\r\n". Proper
 * encoding lines with beginning "." would be a really slow function, not
 * adding any feature to gopher. Clients can check for the types
 * requested and assume ".\r\n" or leave it out.
 *
 * Geomyidae only adds ".\r\n" in all kind of menus, like dir listings
 * or dcgi files. There the case of some maybe future "." item type needs
 * to be handled, if really used.
 */

#include "filetypes.h"

int
pendingbytes(int sock)
{
	int pending, rval;

	pending = 0;
	rval = 0;
#if defined(TIOCOUTQ) && !defined(__OpenBSD__)
	rval = ioctl(sock, TIOCOUTQ, &pending);
#else
#ifdef SIOCOUTQ
	rval = ioctl(sock, SIOCOUTQ, &pending);
#endif
#endif

	if (rval != 0)
		return 0;

	return pending;
}

void
waitforpendingbytes(int sock)
{
	int npending = 0, opending = 0;
	useconds_t trytime = 10;

	/*
	 * Wait until there is nothing pending or the connection stalled
	 * (nothing was sent) for around 40 seconds. Beware, trytime is
	 * an exponential wait.
	 */
	while ((npending = pendingbytes(sock)) > 0 && trytime < 20000000) {
		if (opending != 0) {
			if (opending != npending) {
				trytime = 10;
			} else {
				/*
				 * Exponentially increase the usleep
				 * waiting time to not waste CPU
				 * resources.
				 */
				trytime += trytime;
			}
		}
		opending = npending;

		usleep(trytime);
	}
}

#ifdef __linux__
int
xsplice(int fd, int sock)
{
	int pipefd[2], ret = 0;
	ssize_t nread, nwritten;
	off_t in_offset = 0;

	if (pipe(pipefd) < 0)
		return -1;

	do {
		nread = splice(fd, &in_offset, pipefd[1], NULL,
			BLOCK_SIZE, SPLICE_F_MOVE | SPLICE_F_MORE);

		if (nread <= 0) {
			ret = nread < 0 ? -1 : 0;
			goto out;
		}

		nwritten  = splice(pipefd[0], NULL, sock, NULL, BLOCK_SIZE,
				SPLICE_F_MOVE | SPLICE_F_MORE);
		if (nwritten < 0) {
			ret = -1;
			goto out;
		}
	} while (nwritten > 0);

out:
	close(pipefd[0]);
	close(pipefd[1]);

	return ret;
}
#endif

int
xsendfile(int fd, int sock)
{
	struct stat st;
	char *sendb, *sendi;
	size_t bufsiz = BUFSIZ;
	int len, sent, optval;

#ifdef splice
	return xsplice(fd, sock);
#endif

	USED(optval);

	/*
	 * The story of xsendfile.
	 *
	 * Once upon a time, here you saw a big #ifdef switch source of
	 * many ways how to send files with special functions on
	 * different operating systems. All of this was removed, because
	 * operating systems and kernels got better over time,
	 * simplifying what you need and reducing corner cases.
	 *
	 * For example Linux sendfile(2) sounds nice and faster, but
	 * the function is different on every OS and slower to the now
	 * used approach of read(2) and write(2).
	 *
	 * If you ever consider changing this to some "faster" approach,
	 * consider benchmarks on all platforms.
	 */

	if (fstat(fd, &st) >= 0) {
		if ((bufsiz = st.st_blksize) < BUFSIZ)
			bufsiz = BUFSIZ;
	}

	sendb = xmalloc(bufsiz);
	while ((len = read(fd, sendb, bufsiz)) > 0) {
		sendi = sendb;
		while (len > 0) {
			if ((sent = write(sock, sendi, len)) < 0) {
				free(sendb);
				return -1;
			}
			len -= sent;
			sendi += sent;
		}
	}
	free(sendb);

	return 0;
}

void *
xcalloc(size_t nmemb, size_t size)
{
	void *p;

	if (!(p = calloc(nmemb, size))) {
		perror("calloc");
		exit(1);
	}

	return p;
}

void *
xmalloc(size_t size)
{
	void *p;

	if (!(p = malloc(size))) {
		perror("malloc");
		exit(1);
	}

	return p;
}

void *
xrealloc(void *ptr, size_t size)
{
	if (!(ptr = realloc(ptr, size))) {
		perror("realloc");
		exit(1);
	}

	return ptr;
}

char *
xstrdup(const char *str)
{
	char *ret;

	if (!(ret = strdup(str))) {
		perror("strdup");
		exit(1);
	}

	return ret;
}

filetype *
gettype(char *filename)
{
	char *end;
	int i;

	end = strrchr(filename, '.');
	if (end == NULL)
		return &type[0];
	end++;

	for (i = 0; type[i].end != NULL; i++)
		if (!strcasecmp(end, type[i].end))
			return &type[i];

	return &type[0];
}

void
gph_freeelem(gphelem *e)
{
	if (e != NULL) {
		if (e->e != NULL) {
			for (;e->num > 0; e->num--)
				if (e->e[e->num - 1] != NULL)
					free(e->e[e->num - 1]);
			free(e->e);
		}
		free(e);
	}
	return;
}

void
gph_freeindex(gphindex *i)
{
	if (i != NULL) {
		if (i->n != NULL) {
			for (;i->num > 0; i->num--)
				gph_freeelem(i->n[i->num - 1]);
			free(i->n);
		}
		free(i);
	}

	return;
}

void
gph_addelem(gphelem *e, char *s)
{
	e->num++;
	e->e = xrealloc(e->e, sizeof(char *) * e->num);
	e->e[e->num - 1] = xstrdup(s);

	return;
}

gphelem *
gph_getadv(char *str)
{
	char *b, *e, *o, *bo;
	gphelem *ret;

	ret = xcalloc(1, sizeof(gphelem));

	if (strchr(str, '\t')) {
		gph_addelem(ret, "i");
		gph_addelem(ret, "Happy helping ☃ here: You tried to "
			"output a spurious TAB character. This will "
			"break gopher. Please review your scripts. "
			"Have a nice day!");
		gph_addelem(ret, "Err");
		gph_addelem(ret, "server");
		gph_addelem(ret, "port");

		return ret;
	}

	/* Check for escape sequence. */
	if (str[0] == '[' && str[1] != '|') {
		o = xstrdup(str);
		b = o + 1;
		bo = b;
		while ((e = strchr(bo, '|')) != NULL) {
			if (e != bo && e[-1] == '\\') {
				memmove(&e[-1], e, strlen(e));
				bo = e;
				continue;
			}
			*e = '\0';
			e++;
			gph_addelem(ret, b);
			b = e;
			bo = b;
		}

		e = strchr(b, ']');
		if (e != NULL) {
			*e = '\0';
			gph_addelem(ret, b);
		}
		free(o);

		if (ret->e != NULL && ret->e[0] != NULL &&
				ret->e[0][0] != '\0' && ret->num == 5) {
			return ret;
		}

		/* Invalid entry: Give back the whole line. */
		gph_freeelem(ret);
		ret = xcalloc(1, sizeof(gphelem));
	}

	gph_addelem(ret, "i");
	/* Jump over escape sequence. */
	if (str[0] == '[' && str[1] == '|')
		str += 2;
	gph_addelem(ret, str);
	gph_addelem(ret, "Err");
	gph_addelem(ret, "server");
	gph_addelem(ret, "port");

	return ret;
}

void
gph_addindex(gphindex *idx, gphelem *el)
{
	idx->num++;
	idx->n = xrealloc(idx->n, sizeof(gphelem *) * idx->num);
	idx->n[idx->num - 1] = el;

	return;
}

gphindex *
gph_scanfile(char *fname)
{
	char *ln = NULL;
	size_t linesiz = 0;
	ssize_t n;
	FILE *fp;
	gphindex *ret;
	gphelem *el;

	if (!(fp = fopen(fname, "r")))
		return NULL;

	ret = xcalloc(1, sizeof(gphindex));

	while ((n = getline(&ln, &linesiz, fp)) > 0) {
		if (ln[n - 1] == '\n')
			ln[--n] = '\0';
		el = gph_getadv(ln);
		if (el == NULL)
			continue;

		gph_addindex(ret, el);
	}
	if (ferror(fp))
		perror("getline");
	free(ln);
	fclose(fp);

	if (ret->n == NULL) {
		free(ret);
		return NULL;
	}

	return ret;
}

int
gph_printelem(int fd, gphelem *el, char *file, char *base, char *addr, char *port)
{
	char *path, *p, *argbase, buf[PATH_MAX+1], *argp, *realbase, *rpath;
	int len, blen;

	if (!strcmp(el->e[3], "server")) {
		free(el->e[3]);
		el->e[3] = xstrdup(addr);
	}
	if (!strcmp(el->e[4], "port")) {
		free(el->e[4]);
		el->e[4] = xstrdup(port);
	}

	/*
	 * Ignore if the path is from base, if it might be some h type with
	 * some URL and ignore various types that have different semantics,
	 * do not point to some file or directory.
	 */
	if ((el->e[2][0] != '\0'
	    && el->e[2][0] != '/' /* Absolute Request. */
	    && el->e[0][0] != 'i' /* Informational item. */
	    && el->e[0][0] != '2' /* CSO server */
	    && el->e[0][0] != '3' /* Error */
	    && el->e[0][0] != '8' /* Telnet */
	    && el->e[0][0] != 'w' /* Selector is direct URI. */
	    && el->e[0][0] != 'T') && /* tn3270 */
	    !(el->e[0][0] == 'h' && !strncmp(el->e[2], "URL:", 4))) {
		path = file + strlen(base);

		/* Strip off original gph file name. */
		if ((p = strrchr(path, '/'))) {
			len = strlen(path) - strlen(basename(path));
		} else {
			len = strlen(path);
		}

		/* Strip off arguments for realpath. */
		argbase = strchr(el->e[2], '?');
		if (argbase != NULL) {
			blen = argbase - el->e[2];
		} else {
			blen = strlen(el->e[2]);
		}

		/*
		 * Print everything together. Realpath will resolve it.
		 */
		snprintf(buf, sizeof(buf), "%s%.*s%.*s", base, len,
			path, blen, el->e[2]);

		if ((rpath = realpath(buf, NULL)) &&
				(realbase = realpath(*base? base : "/", NULL)) &&
				!strncmp(realbase, rpath, strlen(realbase))) {
			p = rpath + (*base? strlen(realbase) : 0);

			/*
			 * Do not forget to re-add arguments which were
			 * stripped off.
			 */
			argp = smprintf("%s%s", *p? p : "/", argbase? argbase : "");

			free(el->e[2]);
			el->e[2] = argp;
			free(realbase);
		}
		if (rpath != NULL)
			free(rpath);
	}

	if (dprintf(fd, "%.1s%s\t%s\t%s\t%s\r\n", el->e[0], el->e[1], el->e[2],
			el->e[3], el->e[4]) < 0) {
		perror("printgphelem: dprintf");
		return -1;
	}
	return 0;
}

char *
smprintf(char *fmt, ...)
{
        va_list fmtargs;
        char *ret;
        int size;

        va_start(fmtargs, fmt);
        size = vsnprintf(NULL, 0, fmt, fmtargs);
        va_end(fmtargs);

        ret = xcalloc(1, ++size);
        va_start(fmtargs, fmt);
        vsnprintf(ret, size, fmt, fmtargs);
        va_end(fmtargs);

        return ret;
}

char *
reverselookup(char *host)
{
	struct in_addr hoststr;
	struct hostent *client;
	char *rethost;

	rethost = NULL;

	if (inet_pton(AF_INET, host, &hoststr)) {
		client = gethostbyaddr((const void *)&hoststr,
				sizeof(hoststr), AF_INET);
		if (client != NULL)
			rethost = xstrdup(client->h_name);
	}

	if (rethost == NULL)
		rethost = xstrdup(host);

	return rethost;
}

void
setcgienviron(char *file, char *path, char *port, char *base, char *args,
		char *sear, char *ohost, char *chost, char *bhost, int istls)
{
	/*
	 * TODO: Clean environment from possible unsafe environment variables.
	 *       But then it is the responsibility of the script writer.
	 */
	unsetenv("AUTH_TYPE");
	unsetenv("CONTENT_LENGTH");
	unsetenv("CONTENT_TYPE");
	setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
	/* TODO: Separate, if run like rest.dcgi. */
	setenv("PATH_INFO", file, 1);
	printf("PATH_INFO = %s\n", file);
	setenv("PATH_TRANSLATED", path, 1);
	printf("PATH_TRANSLATED = %s\n", path);

	setenv("QUERY_STRING", args, 1);
	printf("QUERY_STRING = %s\n", args);
	/* legacy compatibility */
	setenv("SELECTOR", args, 1);
	printf("SELECTOR = %s\n", args);
	setenv("REQUEST", args, 1);
	printf("REQUEST = %s\n", args);

	setenv("REMOTE_ADDR", chost, 1);
	printf("REMOTE_ADDR = %s\n", chost);
	/*
	 * Don't do a reverse lookup on every call. Only do when needed, in
	 * the script. The RFC allows us to set the IP to the value.
	 */
	setenv("REMOTE_HOST", chost, 1);
	printf("REMOTE_HOST = %s\n", chost);
	/* Please do not implement identd here. */
	unsetenv("REMOTE_IDENT");
	unsetenv("REMOTE_USER");
	/* Make PHP happy. */
	setenv("REDIRECT_STATUS", "", 1);
	/*
	 * Only GET is possible in gopher. POST emulation would be really
	 * ugly.
	 */
	setenv("REQUEST_METHOD", "GET", 1);
	setenv("SCRIPT_NAME", file, 1);
	printf("SCRIPT_NAME = %s\n", file);
	setenv("SERVER_NAME", ohost, 1);
	printf("SERVER_PORT = %s\n", port);
	setenv("SERVER_PORT", port, 1);
	setenv("SERVER_LISTEN_NAME", bhost, 1);
	printf("SERVER_LISTEN_NAME = %s\n", bhost);
	if (istls) {
		setenv("SERVER_PROTOCOL", "gophers/1.0", 1);
	} else {
		setenv("SERVER_PROTOCOL", "gopher/1.0", 1);
	}
	setenv("SERVER_SOFTWARE", "geomyidae", 1);

	setenv("X_GOPHER_SEARCH", sear, 1);
	printf("X_GOPHER_SEARCH = %s\n", sear);
	/* legacy compatibility */
	setenv("SEARCHREQUEST", sear, 1);
	printf("SEARCHREQUEST = %s\n", sear);

	if (istls) {
		setenv("GOPHERS", "on", 1);
		setenv("HTTPS", "on", 1);
	} else {
		unsetenv("GOPHERS");
		unsetenv("HTTPS");
	}

}

char *
humansize(off_t n)
{
	static char buf[16];
	const char postfixes[] = "BKMGTPE";
	double size;
	int i = 0;

	for (size = n; size >= 1024 && i < strlen(postfixes); i++)
		size /= 1024;

	if (!i) {
		snprintf(buf, sizeof(buf), "%ju%c", (uintmax_t)n,
				postfixes[i]);
	} else {
		snprintf(buf, sizeof(buf), "%.1f%c", size, postfixes[i]);
	}

	return buf;
}

char *
humantime(const time_t *clock)
{
	static char buf[32];
	struct tm *tm;

	tm = localtime(clock);
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M %Z", tm);

	return buf;
}

