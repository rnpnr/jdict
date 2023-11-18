/* See LICENSE for license details. */
#define LEN(a) (sizeof(a) / sizeof(*a))

typedef struct {
	char *s;
	ptrdiff_t len;
} s8;
#define s8(s) (s8){s, LEN(s) - 1}

int s8cmp(s8, s8);
s8 s8dup(void *, ptrdiff_t);
s8 s8trim(s8);
s8 unescape(s8);
void *xreallocarray(void *, size_t, size_t);
void die(const char *, ...);
