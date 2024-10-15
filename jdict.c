/* See LICENSE for license details. */
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <stdint.h>
#include <stddef.h>
typedef uint8_t   u8;
typedef int64_t   i64;
typedef uint64_t  u64;
typedef int32_t   i32;
typedef uint32_t  u32;
typedef uint32_t  b32;
typedef ptrdiff_t size;

#ifdef _DEBUG
#define ASSERT(c) do { __asm("int3; nop"); } while (0)
#else
#define ASSERT(c) {}
#endif

#define ARRAY_COUNT(a) (sizeof(a) / sizeof(*a))
#define ISSPACE(c)     ((c) == ' ' || (c) == '\n' || (c) == '\t')

#define MEGABYTE (1024ULL * 1024ULL)

typedef struct {
	size len;
	u8   *s;
} s8;
#define s8(cstr) (s8){.len = ARRAY_COUNT(cstr) - 1, .s = (u8 *)cstr}

typedef struct {
	u8 *beg, *end;
#ifdef _DEBUG_ARENA
	size min_capacity_remaining;
#endif
} Arena;

#include "yomidict.c"

#define YOMI_TOKS_PER_ENT 10

/* buffer length for interactive mode */
#define BUFLEN 256

/* Number of hash table slots (1 << HT_EXP) */
#define HT_EXP 20

typedef struct DictDef {
	s8 text;
	struct DictDef *next;
} DictDef;

typedef struct {
	s8 term;
	DictDef *def;
} DictEnt;

struct ht {
	DictEnt **ents;
	i32 len;
};

typedef struct {
	const char *rom;
	const char *name;
	struct ht ht;
} Dict;

#include "config.h"

static Arena
os_new_arena(size cap)
{
	Arena a;

	size pagesize = sysconf(_SC_PAGESIZE);
	if (cap % pagesize != 0)
		cap += pagesize - cap % pagesize;

	a.beg = mmap(0, cap, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
	if (a.beg == MAP_FAILED)
		return (Arena){0};
	a.end = a.beg + cap;
#ifdef _DEBUG_ARENA
	a.min_capacity_remaining = cap;
#endif
	return a;
}

static void *
mem_clear(void *p_, u8 c, size len)
{
	u8 *p = p_;
	while (len) p[--len] = c;
	return p;
}


enum arena_flags {
	ARENA_NONE      = 0 << 0,
	ARENA_NO_CLEAR  = 1 << 0,
	ARENA_ALLOC_END = 1 << 1,
};

#define alloc(a, t, n, flags)  (t *)alloc_(a, sizeof(t), _Alignof(t), n, flags)
static void *
alloc_(Arena *a, size len, size align, size count, u32 flags)
{
	size padding;
	if (flags & ARENA_ALLOC_END) padding = -(uintptr_t)a->end & (align - 1);
	else                         padding = -(uintptr_t)a->beg & (align - 1);

	size available = a->end - a->beg - padding;
	if (available <= 0 || available / len <= count)
		ASSERT(0);

	void *result;
	if (flags & ARENA_ALLOC_END) {
		a->end -= padding + count * len;
		result  = a->end;
	} else {
		result  = a->beg + padding;
		a->beg += padding + count * len;
	}

#ifdef _DEBUG_ARENA
	if (a->end - a->beg < a->min_capacity_remaining)
		a->min_capacity_remaining = a->end - a->beg;
#endif

	if (flags & ARENA_NO_CLEAR) return result;
	else                        return mem_clear(result, 0, count * len);
}

static void __attribute__((noreturn))
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	exit(1);
}

static void
usage(char *argv0)
{
	die("usage: %s [-d path] [-F FS] [-i] term ...\n", argv0);
}

static s8
s8_alloc(Arena *a, size len)
{
	s8 result = {.len = len, .s = alloc(a, u8, len, ARENA_NO_CLEAR)};
	return result;
}

static s8
s8_dup(Arena *a, s8 old)
{
	s8 result = s8_alloc(a, old.len);
	for (size i = 0; i < old.len; i++)
		result.s[i] = old.s[i];
	return result;
}

static s8
cstr_to_s8(char *cstr)
{
	s8 result = {.s = (u8 *)cstr};
	if (cstr) while (*cstr) { result.len++; cstr++; }
	return result;
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

	for (; str.len && ISSPACE(*p); str.len--, p--);
	for (; str.len && ISSPACE(*str.s); str.len--, str.s++);

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

/* FNV-1a hash */
static u64
hash(s8 v)
{
	u64 h = 0x3243f6a8885a308d; /* digits of pi */
	for (; v.len; v.len--) {
		h ^= v.s[v.len - 1] & 0xFF;
		h *= 1111111111111111111; /* random prime */
	}
	return h;
}

static i32
ht_lookup(u64 hash, int exp, i32 idx)
{
	u32 mask = ((u32)1 << exp) - 1;
	u32 step = (hash >> (64 - exp)) | 1;
	return (idx + step) & mask;
}

static DictEnt **
intern(struct ht *t, s8 key)
{
	u64 h = hash(key);
	i32 i = h;
	for (;;) {
		i = ht_lookup(h, HT_EXP, i);
		if (!t->ents[i]) {
			/* empty slot */
			#ifdef _DEBUG
			if ((u32)t->len + 1 == (u32)1<<(HT_EXP - 1))
				fputs("intern: ht exceeded 0.5 fill factor\n", stderr);
			#endif
			t->len++;
			return t->ents + i;
		} else if (!s8cmp(t->ents[i]->term, key)) {
			/* found; return the stored instance */
			return t->ents + i;
		}
		/* NOTE: else relookup and try again */
	}
}

static size_t
count_term_banks(const char *path)
{
	DIR *dir;
	struct dirent *dent;
	size_t nbanks = 0;

	if (!(dir = opendir(path)))
		die("opendir(): failed to open: %s\n", path);

	/* count term banks in path */
	while ((dent = readdir(dir)) != NULL)
		if (dent->d_type == DT_REG)
			nbanks++;
	/* remove index.json from count */
	nbanks--;

	closedir(dir);
	return nbanks;
}

static void
parse_term_bank(Arena *a, struct ht *ht, const char *tbank)
{
	Arena start = *a;

	i32 fd = open(tbank, O_RDONLY);
	if (fd < 0)
		die("can't open file: %s\n", tbank);
	size flen = lseek(fd, 0, SEEK_END);
	u8 *data = mmap(NULL, flen, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	if (data == MAP_FAILED)
		die("couldn't mmap file: %s\n", tbank);

	/* allocate tokens */
	size ntoks = (1 << HT_EXP) * YOMI_TOKS_PER_ENT + 1;
	YomiTok *toks = alloc(a, YomiTok, ntoks, ARENA_ALLOC_END|ARENA_NO_CLEAR);

	YomiScanner s = {0};
	yomi_scanner_init(&s, (char *)data, flen);
	i32 r;
	while ((r = yomi_scan(&s, toks, ntoks)) < 0) {
		switch (r) {
		case YOMI_ERROR_NOMEM:
			goto cleanup;
		case YOMI_ERROR_INVAL:
		case YOMI_ERROR_MALFO:
			fprintf(stderr, "yomi_parse: %s\n",
			        r == YOMI_ERROR_INVAL? "YOMI_ERROR_INVAL"
			        : "YOMI_ERROR_MALFO");
			goto cleanup;
		}
	}

	for (i32 i = 0; i < r; i++) {
		YomiTok *base_tok = toks + i;
		if (base_tok->type != YOMI_ENTRY)
			continue;

		YomiTok *tstr = NULL, *tdefs = NULL;
		for (size_t j = 1; j < base_tok->len; j++) {
			switch (base_tok[j].type) {
			case YOMI_STR:
				if (tstr == NULL)
					tstr = base_tok + j;
				break;
			case YOMI_ARRAY:
				if (tdefs == NULL)
					tdefs = base_tok + j;
			default: /* FALLTHROUGH */
				break;
			}
		}

		/* check if entry was valid */
		if (tdefs == NULL || tstr == NULL) {
			fprintf(stderr, "parse_term_bank: %s == NULL\n",
			        tdefs == NULL? "tdefs" : "tstr");
			break;
		}

		s8 mem_term = {.len = tstr->end - tstr->start, .s = data + tstr->start};
		DictEnt **n = intern(ht, mem_term);

		if (!*n) {
			*n         = alloc(a, DictEnt, 1, 0);
			(*n)->term = s8_dup(a, mem_term);
		} else {
			if (s8cmp((*n)->term, mem_term)) {
				fputs("hash collision: ", stderr);
				fwrite(mem_term.s, mem_term.len, 1, stderr);
				fputc('\t', stderr);
				fwrite((*n)->term.s, (*n)->term.len, 1, stderr);
				fputc('\n', stderr);
			}
		}

		for (size_t i = 1; i <= tdefs->len; i++) {
			DictDef *def = alloc(a, DictDef, 1, ARENA_NO_CLEAR);
			def->text = s8_dup(a, (s8){.len = tdefs[i].end - tdefs[i].start,
			                           .s = data + tdefs[i].start});
			def->next = (*n)->def;
			(*n)->def = def;
		}
	}

cleanup:
	munmap(data, flen);

	/* NOTE: clear temporary allocations */
	a->end = start.end;
}

static int
make_dict(Arena *a, Dict *d)
{
	char path[PATH_MAX - 20], tbank[PATH_MAX];
	size_t nbanks;

	d->ht.ents = alloc(a, DictEnt *, 1 << HT_EXP, 0);

	snprintf(path, ARRAY_COUNT(path), "%s/%s", prefix, d->rom);
	if ((nbanks = count_term_banks(path)) == 0) {
		fprintf(stderr, "no term banks found: %s\n", path);
		return 0;
	}

	for (size_t i = 1; i <= nbanks; i++) {
		snprintf(tbank, ARRAY_COUNT(tbank), "%s/term_bank_%zu.json", path, i);
		parse_term_bank(a, &d->ht, tbank);
	}

	return 1;
}

static void
make_dicts(Arena *a, Dict *dicts, size_t ndicts)
{
	for (size_t i = 0; i < ndicts; i++)
		if (!make_dict(a, &dicts[i]))
			die("make_dict(%s): returned NULL\n", dicts[i].rom);
}

static DictEnt *
find_ent(s8 term, Dict *d)
{
	u64 h = hash(term);
	i32 i = ht_lookup(h, HT_EXP, (i32)h);
	return d->ht.ents[i];
}

static void
find_and_print(s8 term, Dict *d)
{
	DictEnt *ent = find_ent(term, d);

	if (!ent || s8cmp(term, ent->term))
		return;

	for (DictDef *def = ent->def; def; def = def->next) {
		if (!s8cmp(fsep, s8("\n")))
			def->text = unescape(def->text);
		fputs(d->name, stdout);
		fwrite(fsep.s, fsep.len, 1, stdout);
		fwrite(def->text.s, def->text.len, 1, stdout);
		fputc('\n', stdout);
	}
}

static void
find_and_print_defs(Arena *a, Dict *dict, s8 *terms, size_t nterms)
{
	size_t i;

	if (!make_dict(a, dict)) {
		fputs("failed to allocate dict: ", stdout);
		puts(dict->rom);
		return;
	}

	for (i = 0; i < nterms; i++)
		find_and_print(terms[i], dict);
}

static void
repl(Arena *a, Dict *dicts, size_t ndicts)
{
	u8 t[BUFLEN];
	s8 buf = {.len = ARRAY_COUNT(t), .s = t};
	size_t i;

	make_dicts(a, dicts, ndicts);

	fsep = s8("\n");
	for (;;) {
		fputs(repl_prompt, stdout);
		fflush(stdout);
		buf.len = ARRAY_COUNT(t);
		if (fgets((char *)buf.s, buf.len, stdin) == NULL)
			break;
		buf.len = strlen((char *)buf.s);
		for (i = 0; i < ndicts; i++)
			find_and_print(s8trim(buf), &dicts[i]);
	}
	puts(repl_quit);
}

int
main(int argc, char *argv[])
{
	Arena memory = os_new_arena(1024 * MEGABYTE);
	Dict *dicts = NULL;
	size_t ndicts = 0, nterms = 0;
	int iflag = 0;

	char *argv0 = argv[0];
	for (argv++, argc--; argv[0] && argv[0][0] == '-' && argv[0][1]; argc--, argv++) {
		/* NOTE: '--' to end parameters */
		if (argv[0][1] == '-' && argv[0][2] == 0) {
			argv++;
			argc--;
			break;
		}
		switch (argv[0][1]) {
		case 'F':
			if (!argv[1] || !argv[1][0])
				usage(argv0);
			fsep = unescape(cstr_to_s8(argv[1]));
			argv++;
			break;
		case 'd':
			if (!argv[1] || !argv[1][0])
				usage(argv0);
			for (u32 j = 0; j < ARRAY_COUNT(default_dict_map); j++) {
				if (strcmp(argv[1], default_dict_map[j].rom) == 0) {
					dicts = &default_dict_map[j];
					ndicts++;
					break;
				}
			}
			if (dicts == NULL)
				die("invalid dictionary name: %s\n", argv[1]);
			argv++;
			break;
		case 'i': iflag = 1;   break;
		default: usage(argv0); break;
		}
	}

	if (ndicts == 0) {
		dicts  = default_dict_map;
		ndicts = ARRAY_COUNT(default_dict_map);
	}

	/* NOTE: remaining argv elements are search terms */
	nterms = argc;
	s8 *terms = alloc(&memory, s8, nterms, 0);
	for (i32 i = 0; argc && *argv; argv++, i++, argc--)
		terms[i] = cstr_to_s8(*argv);

	if (nterms == 0 && iflag == 0)
		usage(argv0);

	if (iflag == 0)
		for (size_t i = 0; i < ndicts; i++)
			find_and_print_defs(&memory, &dicts[i], terms, nterms);
	else
		repl(&memory, dicts, ndicts);

#ifdef _DEBUG_ARENA
	printf("min remaining arena capacity: %zd\n", memory.min_capacity_remaining);
	printf("remaining arena capacity: %zd\n", memory.end - memory.beg);
#endif

	return 0;
}
