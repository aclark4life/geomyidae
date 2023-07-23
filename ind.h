/*
 * Copy me if you can.
 * by 20h
 */

#ifndef IND_H
#define IND_H

#include <stdio.h>

extern int glfd;

typedef struct filetype filetype;
struct filetype {
        char *end;
        char *type;
        void (* f)(int, char *, char *, char *, char *, char *, char *,
		char *, char *, int, char *, char *);
};

filetype *gettype(char *filename);

typedef struct gphelem gphelem;
struct gphelem {
	char **e;
	int num;
};

typedef struct gphindex gphindex;
struct gphindex {
	gphelem **n;
	int num;
};

gphindex *gph_scanfile(char *fname);
gphelem *gph_getadv(char *str);
int gph_printelem(int fd, gphelem *el, char *file, char *base, char *addr, char *port);
void gph_addindex(gphindex *idx, gphelem *el);
void gph_addelem(gphelem *e, char *s);
void gph_freeindex(gphindex *i);
void gph_freeelem(gphelem *e);

void *xcalloc(size_t, size_t);
void *xmalloc(size_t);
void *xrealloc(void *, size_t);
char *xstrdup(const char *str);
int xsendfile(int, int);
int pendingbytes(int sock);
void waitforpendingbytes(int sock);
char *smprintf(char *fmt, ...);
char *reverselookup(char *host);
void setcgienviron(char *file, char *path, char *port, char *base,
		char *args, char *sear, char *ohost, char *chost,
		char *bhost, int istls, char *sel, char *traverse);
char *humansize(off_t n);
char *humantime(const time_t *clock);
void lingersock(int sock);

#endif

