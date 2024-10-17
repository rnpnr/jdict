/* See LICENSE for license details. */
#include <stdint.h>
#include <stddef.h>
typedef uint8_t   u8;
typedef int64_t   i64;
typedef uint64_t  u64;
typedef int32_t   i32;
typedef uint32_t  u32;
typedef uint32_t  b32;
typedef uint16_t  u16;
typedef ptrdiff_t size;
typedef ptrdiff_t iptr;

#ifdef _DEBUG
#define ASSERT(c) do { __asm("int3; nop"); } while (0)
#else
#define ASSERT(c) {}
#endif

#ifndef unreachable
#define unreachable() __builtin_unreachable()
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
	u8   *data;
	u32   cap;
	u32   widx;
	i32   fd;
	b32   errors;
} Stream;

typedef struct {
	u8 *beg, *end;
#ifdef _DEBUG_ARENA
	size min_capacity_remaining;
#endif
} Arena;

typedef struct {
	Stream *dir_name;
	void   *dirfd;
} PathStream;

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
	s8 rom;
	s8 name;
	struct ht ht;
} Dict;

#include "config.h"

static void __attribute__((noreturn)) os_exit(i32);

static void os_write(iptr, s8);
static s8   os_read_whole_file(char *, Arena *, u32);
static b32  os_read_stdin(u8 *, size);

static PathStream os_begin_path_stream(Stream *, Arena *, u32);
static s8         os_get_valid_file(PathStream *, s8, Arena *, u32);
static void       os_end_path_stream(PathStream *);

static Stream error_stream;
static Stream stdout_stream;

static void
stream_append_byte(Stream *s, u8 b)
{
	s->errors |= s->widx + 1 > s->cap;
	if (!s->errors)
		s->data[s->widx++] = b;
}

static void
stream_append_s8(Stream *s, s8 str)
{
	s->errors |= (s->cap - s->widx) < str.len;
	if (!s->errors) {
		for (size i = 0; i < str.len; i++)
			s->data[s->widx++] = str.s[i];
	}
}

#ifdef _DEBUG_ARENA
static void
stream_append_u64(Stream *s, u64 n)
{
	u8 tmp[64];
	u8 *end = tmp + sizeof(tmp);
	u8 *beg = end;
	do { *--beg = '0' + (n % 10); } while (n /= 10);
	stream_append_s8(s, (s8){.len = end - beg, .s = beg});
}
#endif

static void
stream_flush(Stream *s)
{
	if (s->widx) {
		os_write(s->fd, (s8){.len = s->widx, .s = s->data});
		s->widx   = 0;
		s->errors = 0;
	}
}

static s8
cstr_to_s8(char *cstr)
{
	s8 result = {.s = (u8 *)cstr};
	if (cstr) while (*cstr) { result.len++; cstr++; }
	return result;
}

static void __attribute__((noreturn))
die(Stream *s)
{
	if (s->data[s->widx - 1] != '\n')
		stream_append_byte(s, '\n');
	stream_flush(s);
	os_exit(1);
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

static void
usage(s8 argv0)
{
	stream_append_s8(&error_stream, s8("usage: "));
	stream_append_s8(&error_stream, argv0);
	stream_append_s8(&error_stream, s8(" [-d path] [-F FS] [-i] term ...\n"));
	die(&error_stream);
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

static i32
s8_cmp(s8 a, s8 b)
{
	if (a.len == 0 || a.len != b.len)
		return a.len - b.len;
	i32 result = 0;
	/* NOTE: we assume short strings in this program */
	for (size i = 0; i < a.len; i++)
		result += b.s[i] - a.s[i];
	return result;
}

static s8
s8_cut_head(s8 s, size count)
{
	s8 result   = s;
	result.s   += count;
	result.len -= count;
	return result;
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
	for (size i = 0; i < str.len; i++) {
		if (str.s[i] == '\\') {
			switch (str.s[i + 1]) {
			case 'n': str.s[i] = '\n'; break;
			case 't': str.s[i] = '\t'; break;
			default: continue;
			}
			str.len--;
			for (size j = i + 1; j < str.len; j++)
				str.s[j] = str.s[j + 1];
		}
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
			if ((u32)t->len + 1 == (u32)1<<(HT_EXP - 1)) {
				stream_append_s8(&error_stream,
				                 s8("intern: ht exceeded 0.5 fill factor\n"));
				stream_flush(&error_stream);
			}
			#endif
			t->len++;
			return t->ents + i;
		} else if (!s8_cmp(t->ents[i]->term, key)) {
			/* found; return the stored instance */
			return t->ents + i;
		}
		/* NOTE: else relookup and try again */
	}
}

static void
parse_term_bank(Arena *a, struct ht *ht, s8 data)
{
	/* allocate tokens */
	size ntoks = (1 << HT_EXP) * YOMI_TOKS_PER_ENT + 1;
	YomiTok *toks = alloc(a, YomiTok, ntoks, ARENA_ALLOC_END|ARENA_NO_CLEAR);

	YomiScanner s = {0};
	yomi_scanner_init(&s, (char *)data.s, data.len);
	i32 r;
	while ((r = yomi_scan(&s, toks, ntoks)) < 0) {
		switch (r) {
		case YOMI_ERROR_NOMEM:
			goto cleanup;
		case YOMI_ERROR_INVAL:
		case YOMI_ERROR_MALFO:
			stream_append_s8(&error_stream, s8("yomi_parse: "));
			if (r == YOMI_ERROR_INVAL)
				stream_append_s8(&error_stream, s8("YOMI_ERROR_INVAL\n"));
			else
				stream_append_s8(&error_stream, s8("YOMI_ERROR_MALFO\n"));
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
			stream_append_s8(&error_stream, s8("parse_term_bank: "));
			if (!tdefs)
				stream_append_s8(&error_stream, s8("tdefs"));
			else
				stream_append_s8(&error_stream, s8("tstr"));
			stream_append_s8(&error_stream, s8(" == NULL\n"));
			break;
		}

		s8 mem_term = {.len = tstr->end - tstr->start, .s = data.s + tstr->start};
		DictEnt **n = intern(ht, mem_term);

		if (!*n) {
			*n         = alloc(a, DictEnt, 1, 0);
			(*n)->term = s8_dup(a, mem_term);
		} else {
			if (s8_cmp((*n)->term, mem_term)) {
				stream_append_s8(&error_stream, s8("hash collision: "));
				stream_append_s8(&error_stream, mem_term);
				stream_append_byte(&error_stream, '\t');
				stream_append_s8(&error_stream, (*n)->term);
				stream_append_byte(&error_stream, '\n');
				stream_flush(&error_stream);
			}
		}

		for (size_t i = 1; i <= tdefs->len; i++) {
			DictDef *def = alloc(a, DictDef, 1, ARENA_NO_CLEAR);
			def->text = s8_dup(a, (s8){.len = tdefs[i].end - tdefs[i].start,
			                           .s = data.s + tdefs[i].start});
			def->next = (*n)->def;
			(*n)->def = def;
		}
	}

cleanup:
	stream_flush(&error_stream);
}

static int
make_dict(Arena *a, Dict *d)
{
	u8 *starting_arena_end = a->end;
	Stream path = {.cap = 1 * MEGABYTE};
	path.data   = alloc(a, u8, path.cap, ARENA_ALLOC_END|ARENA_NO_CLEAR);
	d->ht.ents  = alloc(a, DictEnt *, 1 << HT_EXP, 0);

	stream_append_s8(&path, prefix);
	stream_append_s8(&path, os_path_sep);
	stream_append_s8(&path, d->rom);
	PathStream ps = os_begin_path_stream(&path, a, ARENA_ALLOC_END);

	u8 *arena_end = a->end;
	s8 fn_pre = s8("term");
	for (s8 filedata = os_get_valid_file(&ps, fn_pre, a, ARENA_ALLOC_END);
	     filedata.len;
	     filedata = os_get_valid_file(&ps, fn_pre, a, ARENA_ALLOC_END))
	{
		parse_term_bank(a, &d->ht, filedata);
		a->end = arena_end;
	}
	os_end_path_stream(&ps);

	a->end = starting_arena_end;

	return 1;
}

static void
make_dicts(Arena *a, Dict *dicts, size_t ndicts)
{
	for (size_t i = 0; i < ndicts; i++) {
		if (!make_dict(a, &dicts[i])) {
			stream_append_s8(&error_stream, s8("make_dict failed for: "));
			stream_append_s8(&error_stream, dicts[i].rom);
			die(&error_stream);
		}
	}
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

	if (!ent || s8_cmp(term, ent->term))
		return;

	for (DictDef *def = ent->def; def; def = def->next) {
		if (!s8_cmp(fsep, s8("\n")))
			def->text = unescape(def->text);
		stream_append_s8(&stdout_stream, d->name);
		stream_append_s8(&stdout_stream, fsep);
		stream_append_s8(&stdout_stream, def->text);
		stream_append_byte(&stdout_stream, '\n');
		stream_flush(&stdout_stream);
	}
}

static void
find_and_print_defs(Arena *a, Dict *dict, s8 *terms, size_t nterms)
{
	if (!make_dict(a, dict)) {
		stream_append_s8(&error_stream, s8("failed to allocate dict: "));
		stream_append_s8(&error_stream, dict->rom);
		stream_append_byte(&stdout_stream, '\n');
		stream_flush(&error_stream);
		return;
	}

	for (size_t i = 0; i < nterms; i++)
		find_and_print(terms[i], dict);
}

static b32
get_stdin_line(Stream *buf)
{
	b32 result = 0;
	for (; buf->widx < buf->cap; buf->widx++) {
		u8 *c = buf->data + buf->widx;
		if (!os_read_stdin(c, 1) || *c == (u8)-1) {
			break;
		} else if (*c == '\n') {
			result = 1;
			break;
		}
	}
	return result;
}

static void
repl(Arena *a, Dict *dicts, size_t ndicts)
{
	Stream buf = {.cap = 4096};
	buf.data   = alloc(a, u8, buf.cap, ARENA_NO_CLEAR);

	make_dicts(a, dicts, ndicts);

	fsep = s8("\n");
	for (;;) {
		stream_append_s8(&stdout_stream, repl_prompt);
		stream_flush(&stdout_stream);
		if (!get_stdin_line(&buf))
			break;
		s8 trimmed = s8trim((s8){.len = buf.widx, .s = buf.data});
		for (size_t i = 0; i < ndicts; i++)
			find_and_print(trimmed, &dicts[i]);
		buf.widx = 0;
	}
	stream_append_s8(&stdout_stream, repl_quit);
	if (repl_quit.s[repl_quit.len - 1] != '\n')
		stream_append_byte(&stdout_stream, '\n');
	stream_flush(&stdout_stream);
}

static i32
jdict(Arena *a, i32 argc, char *argv[])
{
	Dict *dicts = NULL;
	i32 ndicts = 0, nterms = 0;
	i32 iflag = 0;

	s8 argv0 = cstr_to_s8(argv[0]);
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
		case 'd': {
			if (!argv[1] || !argv[1][0])
				usage(argv0);
			s8 dname = cstr_to_s8(argv[1]);
			for (u32 j = 0; j < ARRAY_COUNT(default_dict_map); j++) {
				if (s8_cmp(dname, default_dict_map[j].rom) == 0) {
					dicts = &default_dict_map[j];
					ndicts++;
					break;
				}
			}
			if (dicts == NULL) {
				stream_append_s8(&error_stream, s8("invalid dictionary name: "));
				stream_append_s8(&error_stream, dname);
				die(&error_stream);
			}
			argv++;
		} break;
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
	s8 *terms = alloc(a, s8, nterms, 0);
	for (i32 i = 0; argc && *argv; argv++, i++, argc--)
		terms[i] = cstr_to_s8(*argv);

	if (nterms == 0 && iflag == 0)
		usage(argv0);

	if (iflag == 0)
		for (i32 i = 0; i < ndicts; i++)
			find_and_print_defs(a, &dicts[i], terms, nterms);
	else
		repl(a, dicts, ndicts);

#ifdef _DEBUG_ARENA
	stream_append_s8(&error_stream, s8("min remaining arena capacity: "));
	stream_append_u64(&error_stream, memory.min_capacity_remaining);
	stream_append_s8(&error_stream, s8("\nremaining arena capacity: "));
	stream_append_u64(&error_stream, memory.end - memory.beg);
	stream_append_byte(&error_stream, '\n');
	stream_flush(&error_stream);
#endif

	return 0;
}
