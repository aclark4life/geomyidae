/*
 * Copy me if you can.
 * by 20h
 */

#include <unistd.h>
#include <memory.h>
#include <netdb.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/wait.h>
#include <errno.h>
#include <libgen.h>

#include "ind.h"
#include "arg.h"

void
handledir(int sock, char *path, char *port, char *base, char *args,
		char *sear, char *ohost, char *chost, char *bhost, int istls,
		char *sel, char *traverse)
{
	char *pa, *file, *e, *par;
	struct dirent **dirent;
	int ndir, i, ret = 0;
	struct stat st;
	filetype *type;

	USED(args);
	USED(sear);
	USED(bhost);
	USED(sel);
	USED(traverse);

	pa = xstrdup(path);

	/* Is there any directory below the request? */
	if (strlen(pa+strlen(base)) > 1) {
		par = xstrdup(pa+strlen(base));
		e = strrchr(par, '/');
		*e = '\0';
		dprintf(sock, "1..\t%s\t%s\t%s\r\n",
			par, ohost, port);
		free(par);
	}

	ndir = scandir(pa, &dirent, 0, alphasort);
	if (ndir < 0) {
		perror("scandir");
		free(pa);
		return;
	} else {
		for (i = 0; i < ndir && ret >= 0; i++) {
			if (dirent[i]->d_name[0] == '.')
				continue;

			type = gettype(dirent[i]->d_name);

			file = smprintf("%s%s%s",
					pa,
					pa[strlen(pa)-1] == '/'? "" : "/",
					dirent[i]->d_name);
			if (stat(file, &st) >= 0 && S_ISDIR(st.st_mode))
				type = gettype("index.gph");
			ret = dprintf(sock,
					"%c%-50.50s %10s %16s\t%s\t%s\t%s\r\n",
					*type->type,
					dirent[i]->d_name,
					humansize(st.st_size),
					humantime(&(st.st_mtime)),
					file + strlen(base), ohost, port);
			free(file);
		}
		for (i = 0; i < ndir; i++)
			free(dirent[i]);
		free(dirent);
	}
	dprintf(sock, ".\r\n");

	free(pa);
}

void
handlegph(int sock, char *file, char *port, char *base, char *args,
		char *sear, char *ohost, char *chost, char *bhost, int istls,
		char *sel, char *traverse)
{
	gphindex *act;
	int i, ret = 0;

	USED(args);
	USED(sear);
	USED(bhost);
	USED(sel);
	USED(traverse);

	act = gph_scanfile(file);
	if (act != NULL) {
		for (i = 0; i < act->num && ret >= 0; i++)
			ret = gph_printelem(sock, act->n[i], file, base, ohost, port);
		dprintf(sock, ".\r\n");

		for (i = 0; i < act->num; i++) {
			gph_freeelem(act->n[i]);
			act->n[i] = NULL;
		}
		gph_freeindex(act);
	}
}

void
handlebin(int sock, char *file, char *port, char *base, char *args,
		char *sear, char *ohost, char *chost, char *bhost, int istls,
		char *sel, char *traverse)
{
	int fd;

	USED(port);
	USED(base);
	USED(args);
	USED(sear);
	USED(ohost);
	USED(bhost);
	USED(sel);
	USED(traverse);

	fd = open(file, O_RDONLY);
	if (fd >= 0) {
		if (xsendfile(fd, sock) < 0)
			perror("sendfile");
		close(fd);
	}
}

void
handlecgi(int sock, char *file, char *port, char *base, char *args,
		char *sear, char *ohost, char *chost, char *bhost, int istls,
		char *sel, char *traverse)
{
	char *script, *path, *filec;

	USED(base);
	USED(port);

	printf("handlecgi:\n");
	printf("sock = %d; file = %s; port = %s; base = %s; args = %s;\n",
			sock, file, port, base, args);
	printf("sear = %s; ohost = %s; chost = %s; bhost = %s; istls = %d;\n",
			sear, ohost, chost, bhost, istls);
	printf("sel = %s; traverse = %s;\n", sel, traverse);

	filec = xstrdup(file);
	path = dirname(filec);
	script = path + strlen(path) + 1;

	if (sear == NULL)
		sear = "";
	if (args == NULL)
		args = "";

	while (dup2(sock, 0) < 0 && errno == EINTR);
	while (dup2(sock, 1) < 0 && errno == EINTR);
	while (dup2(sock, 2) < 0 && errno == EINTR);
	switch (fork()) {
	case 0:
		if (path != NULL) {
			if (chdir(path) < 0)
				break;
		}

		setcgienviron(script, file, port, base, args, sear, ohost, chost,
				bhost, istls, sel, traverse);

		if (execl(file, script, sear, args, ohost, port,
				(char *)NULL) == -1) {
			perror("execl");
			_exit(1);
		}
	case -1:
		perror("fork");
		break;
	default:
		wait(NULL);
		free(filec);
		break;
	}
}

void
handledcgi(int sock, char *file, char *port, char *base, char *args,
		char *sear, char *ohost, char *chost, char *bhost, int istls,
		char *sel, char *traverse)
{
	FILE *fp;
	char *script, *path, *filec, *ln = NULL;
	size_t linesiz = 0;
	ssize_t n;
	int outsocks[2], ret = 0;
	gphelem *el;

	printf("handledcgi:\n");
	printf("sock = %d; file = %s; port = %s; base = %s; args = %s;\n",
			sock, file, port, base, args);
	printf("sear = %s; ohost = %s; chost = %s; bhost = %s; istls = %d;\n",
			sear, ohost, chost, bhost, istls);
	printf("sel = %s; traverse = %s;\n", sel, traverse);

	if (socketpair(AF_LOCAL, SOCK_STREAM, 0, outsocks) < 0)
		return;

	filec = xstrdup(file);
	path = dirname(filec);
	script = path + strlen(path) + 1;

	if (sear == NULL)
		sear = "";
	if (args == NULL)
		args = "";

	while (dup2(sock, 0) < 0 && errno == EINTR);
	while (dup2(sock, 2) < 0 && errno == EINTR);
	switch (fork()) {
	case 0:
		while (dup2(outsocks[1], 1) < 0 && errno == EINTR);
		close(outsocks[0]);
		if (path != NULL) {
			if (chdir(path) < 0)
				break;
		}

		setcgienviron(script, file, port, base, args, sear, ohost, chost,
				bhost, istls, sel, traverse);

		if (execl(file, script, sear, args, ohost, port,
				(char *)NULL) == -1) {
			perror("execl");
			_exit(1);
		}
		break;
	case -1:
		perror("fork");
		break;
	default:
		while (dup2(sock, 1) < 0 && errno == EINTR);
		close(outsocks[1]);

		if (!(fp = fdopen(outsocks[0], "r"))) {
			perror("fdopen");
			close(outsocks[0]);
			break;
		}

		while ((n = getline(&ln, &linesiz, fp)) > 0 && ret >= 0) {
			if (ln[n - 1] == '\n')
				ln[--n] = '\0';

			el = gph_getadv(ln);
			if (el == NULL)
				continue;

			ret = gph_printelem(sock, el, file, base, ohost, port);
			gph_freeelem(el);
		}
		if (ferror(fp))
			perror("getline");
		dprintf(sock, ".\r\n");

		free(ln);
		fclose(fp);
		wait(NULL);
		free(filec);
		break;
	}
}

