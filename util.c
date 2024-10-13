/* See LICENSE for license details. */
#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h>
#include <stddef.h>
typedef uint8_t   u8;
typedef int64_t   i64;
typedef uint64_t  u64;
typedef int32_t   i32;
typedef uint32_t  u32;
typedef ptrdiff_t size;

#ifdef _DEBUG
#define ASSERT(c) do { __asm("int3; nop"); } while (0)
#else
#define ASSERT(c) {}
#endif

#define ARRAY_COUNT(a) (sizeof(a) / sizeof(*a))

typedef struct {
	size len;
	u8   *s;
} s8;
#define s8(cstr) (s8){.len = ARRAY_COUNT(cstr) - 1, .s = (u8 *)cstr}

static void __attribute__((noreturn))
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	exit(1);
}

static int
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
static s8
s8trim(s8 str)
{
	u8 *p = str.s + str.len - 1;

	for (; str.len && isspace(*p); str.len--, p--);
	for (; str.len && isspace(*str.s); str.len--, str.s++);

	return str;
}

/* replace escaped control chars with their actual char */
static s8
unescape(s8 str)
{
	u8 *t    = str.s;
	size rem = str.len;
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

static void *
xreallocarray(void *o, size_t n, size_t s)
{
	void *new;

	if (!(new = reallocarray(o, n, s)))
		die("reallocarray()\n");

	return new;
}

static s8
s8dup(s8 old)
{
	s8 str = {.len = old.len};
	ASSERT(old.len >= 0);
	if (old.len) {
		str.s = xreallocarray(NULL, 1, old.len);
		memcpy(str.s, old.s, old.len);
	}
	return str;
}

static s8
cstr_to_s8(char *cstr)
{
	s8 result = {.s = (u8 *)cstr};
	if (cstr) while (*cstr) { result.len++; cstr++; }
	return result;
}
