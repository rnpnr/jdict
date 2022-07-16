/* See LICENSE for license details. */
#define LEN(a) (sizeof(a) / sizeof(*a))

void die(const char *, ...);
void trim(char *);
void *xreallocarray(void *, size_t, size_t);
