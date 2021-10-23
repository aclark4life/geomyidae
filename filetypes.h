/*
 * Copy me if you can.
 * by 20h
 */

#ifndef FILETYPES_H
#define FILETYPES_H

#include "ind.h"

/*
 * Before adding any filetype, see the comment in ind.c.
 */

filetype type[] = {
	{"default", "9", handlebin},
	{"gph", "1", handlegph},
	{"cgi", "0", handlecgi},
	{"dcgi", "1", handledcgi},
	{"bin", "9", handlebin},
	{"tgz", "9", handlebin},
	{"gz", "9", handlebin},
	{"jpg", "I", handlebin},
	{"gif", "g", handlebin},
	{"png", "I", handlebin},
	{"bmp", "I", handlebin},
	{"txt", "0", handlebin},
	{"vtt", "0", handlebin},
	{"html", "0", handlebin},
	{"htm", "0", handlebin},
	{"xhtml", "0", handlebin},
	{"css", "0", handlebin},
	{"md", "0", handlebin},
	{"c", "0", handlebin},
	{"sh", "0", handlebin},
	{"patch", "0", handlebin},
	{"meme", "0", handlebin},
	{NULL, NULL, NULL},
};

#endif

