/*
 * Copy me if you can.
 * by 20h
 */

#ifndef HANDLR_H
#define HANDLR_H

/*
 * Handler API definition
 *
 *   Sample: /get/some/script/with/dirs////?key=value\tsearch what?\r\n
 *         * in /get/some/script is a file "index.dcgi"
 *         * request to bitreich.org on port 70 using TLS
 *         * base is in /var/gopher
 *         * client from 85.65.4.2
 *
 * path/file absolute path to the script/directory, always starts with '/'
 *   Sample: /var/gopher/get/some/script/index.dcgi
 * port .... port which the script should use when defining menu items
 *	     (See -o and -p in geomyidae(8))
 *   Sample: 70
 * base .... base path of geomyidae, never ends in '/', so chroot is ''
 *   Sample: /var/gopher
 * args .... Gives all variable input from the selector in some way.
 *   Sample: /with/dirs////?key=value
 * sear .... search part of request
 *   Sample: search what?
 * ohost ... host of geomyidae (See -h in geomyidae(8))
 *   Sample: bitreich.org
 * chost ... IP of the client sending a request
 *   Sample: 85.65.4.2
 * bhost ... server IP the server received the connection to
 *   Sample: 78.46.175.99
 * istls ... set to 1, if TLS was used for thr request
 *   Sample: 1
 */

void handledir(int sock, char *path, char *port, char *base, char *args,
			char *sear, char *ohost, char *chost, char *bhost,
			int istls);
void handlegph(int sock, char *file, char *port, char *base, char *args,
			char *sear, char *ohost, char *chost, char *bhost,
			int istls);
void handlebin(int sock, char *file, char *port, char *base, char *args,
			char *sear, char *ohost, char *chost, char *bhost,
			int istls);
void handletxt(int sock, char *file, char *port, char *base, char *args,
			char *sear, char *ohost, char *chost, char *bhost,
			int istls);
void handlecgi(int sock, char *file, char *port, char *base, char *args,
			char *sear, char *ohost, char *chost, char *bhost,
			int istls);
void handledcgi(int sock, char *file, char *port, char *base, char *args,
			char *sear, char *ohost, char *chost, char *bhost,
			int istls);

#endif
