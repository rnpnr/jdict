/* See LICENSE for license details. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

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

void *
xreallocarray(void *o, size_t n, size_t s)
{
	void *new;

	if (!(new = reallocarray(o, n, s)))
		die("reallocarray()\n");

	return new;
}
