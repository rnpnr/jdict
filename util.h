/* See LICENSE for license details. */
#define LEN(a) (sizeof(a) / sizeof(*a))

void die(const char *, ...);
char *unescape(char *);
char *trim(char *);
char *xmemdup(void *, ptrdiff_t);
void *xreallocarray(void *, size_t, size_t);
