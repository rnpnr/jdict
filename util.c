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

int
s8cmp(s8 a, s8 b)
{
	if (a.len == 0 || a.len != b.len)
		return a.len - b.len;
	return memcmp(a.s, b.s, a.len);
}

/*
 * trim whitespace from start and end of str
 * returns a new s8 (same memory)
 */
s8
s8trim(s8 str)
{
	char *p = &str.s[str.len-1];

	for (; str.len && isspace(*p); str.len--, p--);
	for (; str.len && isspace(*str.s); str.len--, str.s++);

	return str;
}

/* replace escaped control chars with their actual char */
s8
unescape(s8 str)
{
	char *t = str.s;
	ptrdiff_t rem = str.len;
	int off;

	while ((t = memchr(t, '\\', rem)) != NULL) {
		off = 1;
		switch (t[1]) {
		case 'n': t[0] = '\n'; t++; break;
		case 't': t[0] = '\t'; t++; break;
		case 'u': t++; continue;
		default: off++;
		}
		rem = str.len-- - (t - str.s) - off;
		memmove(t, t + off, rem);
	}

	return str;
}

void *
xreallocarray(void *o, size_t n, size_t s)
{
	void *new;

	if (!(new = reallocarray(o, n, s)))
		die("reallocarray()\n");

	return new;
}

s8
s8dup(void *src, ptrdiff_t len)
{
	s8 str = {0, len};
	if (len < 0)
		die("s8dup(): negative len\n");
	str.s = xreallocarray(NULL, 1, len);
	memcpy(str.s, src, len);
	return str;
}
