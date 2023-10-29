/* See LICENSE for license details. */
#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	exit(1);
}

/*
 * trim whitespace from start and end of str
 * returns start of trimmed str
 */
char *
trim(char *s)
{
	char *p = &s[strlen(s)-1];

	for (; isspace(*p); *p = 0, p--);
	for (; *s && isspace(*s); s++);

	return s;
}

/* replace embedded escaped newlines with actual newlines */
char *
fix_newlines(char *s)
{
	char *t = s;

	while ((t = strstr(t, "\\n")) != NULL) {
		t[0] = '\n';
		t++;
		memmove(t, t + 1, strlen(t + 1) + 1);
	}

	return s;
}

void *
xreallocarray(void *o, size_t n, size_t s)
{
	void *new;

	if (!(new = reallocarray(o, n, s)))
		die("reallocarray()\n");

	return new;
}

char *
xmemdup(void *src, ptrdiff_t len)
{
	char *p;
	if (len < 0)
		die("xmemdup(): negative len\n");
	p = xreallocarray(NULL, 1, len + 1);
	p[len] = 0;
	return memcpy(p, src, len);
}
