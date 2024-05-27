/* See LICENSE for license details. */
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "arg.h"
#include "util.c"
#include "yomidict.c"

#define YOMI_TOKS_PER_ENT 10

/* buffer length for interactive mode */
#define BUFLEN 256

/* Number of hash table slots (1 << HT_EXP) */
#define HT_EXP 20

typedef uint64_t u64;
typedef uint32_t u32;
typedef int32_t  i32;

typedef struct {
	s8 term;
	s8 *defs;
	size_t ndefs;
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

char *argv0;

static void
usage(void)
{
	die("usage: %s [-d path] [-F FS] [-i] term ...\n", argv0);
}

static void
merge_ents(DictEnt *a, DictEnt *b)
{
	size_t i, nlen = a->ndefs + b->ndefs;

	if (nlen == 0)
		return;

	a->defs = xreallocarray(a->defs, nlen, sizeof(s8));

	for (i = 0; i < b->ndefs; i++)
		a->defs[a->ndefs + i] = b->defs[i];
	a->ndefs = nlen;
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

static DictEnt *
intern(struct ht *t, DictEnt *e)
{
	s8 key = e->term;
	u64 h = hash(key);
	i32 i = h;
	for (;;) {
		i = ht_lookup(h, HT_EXP, i);
		if (!t->ents[i]) {
			/* empty slot */
			if ((u32)t->len + 1 == (u32)1<<(HT_EXP - 1)) {
				fputs("intern: ht exceeded 0.5 fill factor\n", stderr);
				return NULL;
			}
			t->len++;
			t->ents[i] = e;
			return e;
		} else if (!s8cmp(t->ents[i]->term, e->term)) {
			/* found; return the stored instance */
			return t->ents[i];
		}
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

/* takes a token of type YOMI_ENTRY and creates a DictEnt */
static DictEnt *
make_ent(YomiTok *toks, char *data)
{
	size_t i;
	DictEnt *d;
	YomiTok *tstr = NULL, *tdefs = NULL;

	if (toks[0].type != YOMI_ENTRY) {
		fprintf(stderr, "toks[0].type = %d\n", toks[0].type);
		return NULL;
	}

	for (i = 1; i < toks[0].len; i++)
		switch (toks[i].type) {
		case YOMI_STR:
			if (tstr == NULL)
				tstr = &toks[i];
			break;
		case YOMI_ARRAY:
			if (tdefs == NULL)
				tdefs = &toks[i];
		default: /* FALLTHROUGH */
			break;
		}

	/* check if entry was valid */
	if (tdefs == NULL || tstr == NULL) {
		fprintf(stderr, "make_ent: %s == NULL\n",
		        tdefs == NULL? "tdefs" : "tstr");
		return NULL;
	}

	d = xreallocarray(NULL, 1, sizeof(DictEnt));
	d->term = s8dup(data + tstr->start, tstr->end - tstr->start);
	d->ndefs = tdefs->len;
	d->defs = xreallocarray(NULL, d->ndefs, sizeof(s8));
	for (i = 1; i <= d->ndefs; i++)
		d->defs[i - 1] = s8dup(data + tdefs[i].start,
		                       tdefs[i].end - tdefs[i].start);

	return d;
}

static void
parse_term_bank(struct ht *ht, const char *tbank)
{
	int i = 0, r, ntoks, fd;
	size_t flen;
	char *data;
	YomiTok *toks = NULL;
	YomiScanner s = {0};
	DictEnt *e, *n;

	if ((fd = open(tbank, O_RDONLY)) < 0)
		die("can't open file: %s\n", tbank);
	flen = lseek(fd, 0, SEEK_END);
	data = mmap(NULL, flen, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	if (data == MAP_FAILED)
		die("couldn't mmap file: %s\n", tbank);

	/* allocate tokens */
	ntoks = (1 << HT_EXP) * YOMI_TOKS_PER_ENT + 1;
	toks = xreallocarray(toks, ntoks, sizeof(YomiTok));

	yomi_scanner_init(&s, data, flen);
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

	for (i = 0; i < r; i++) {
		if (toks[i].type != YOMI_ENTRY)
			continue;

		if ((e = make_ent(&toks[i], data)) == NULL)
			break;
		if ((n = intern(ht, e)) == NULL)
			break;
		if (n == e)
			continue;
		/* hash table entry already exists, append new defs */
		if (s8cmp(n->term, e->term)) {
			fputs("hash collision: ", stderr);
			fwrite(e->term.s, e->term.len, 1, stderr);
			fputc('\t', stderr);
			fwrite(n->term.s, n->term.len, 1, stderr);
			fputc('\n', stderr);
		}
		merge_ents(n, e);
		free(e->term.s);
		free(e->defs);
		free(e);
	}

cleanup:
	munmap(data, flen);
	free(toks);
}

static int
make_dict(Dict *d)
{
	char path[PATH_MAX - 20], tbank[PATH_MAX];
	size_t nbanks;

	d->ht.ents = xreallocarray(NULL, sizeof(DictEnt *), 1 << HT_EXP);

	snprintf(path, LEN(path), "%s/%s", prefix, d->rom);
	if ((nbanks = count_term_banks(path)) == 0) {
		fprintf(stderr, "no term banks found: %s\n", path);
		return 0;
	}

	for (size_t i = 1; i <= nbanks; i++) {
		snprintf(tbank, LEN(tbank), "%s/term_bank_%zu.json", path, i);
		parse_term_bank(&d->ht, tbank);
	}

	return 1;
}

static void
make_dicts(Dict *dicts, size_t ndicts)
{
	for (size_t i = 0; i < ndicts; i++)
		if (!make_dict(&dicts[i]))
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
	size_t i;

	if (!ent || s8cmp(term, ent->term))
		return;

	for (i = 0; i < ent->ndefs; i++) {
		if (!s8cmp(fsep, s8("\n")))
			ent->defs[i] = unescape(ent->defs[i]);
		fputs(d->name, stdout);
		fwrite(fsep.s, fsep.len, 1, stdout);
		fwrite(ent->defs[i].s, ent->defs[i].len, 1, stdout);
		fputc('\n', stdout);
	}
}

static void
find_and_print_defs(Dict *dict, s8 *terms, size_t nterms)
{
	size_t i;

	if (!make_dict(dict)) {
		fputs("failed to allocate dict: ", stdout);
		puts(dict->rom);
		return;
	}

	for (i = 0; i < nterms; i++)
		find_and_print(terms[i], dict);
}

static void
repl(Dict *dicts, size_t ndicts)
{
	char t[BUFLEN];
	s8 buf = {t, BUFLEN};
	size_t i;

	make_dicts(dicts, ndicts);

	fsep = s8("\n");
	for (;;) {
		fputs(repl_prompt, stdout);
		fflush(stdout);
		buf.len = BUFLEN;
		if (fgets(buf.s, buf.len, stdin) == NULL)
			break;
		buf.len = strlen(buf.s);
		for (i = 0; i < ndicts; i++)
			find_and_print(s8trim(buf), &dicts[i]);
	}
	puts(repl_quit);
}

int
main(int argc, char *argv[])
{
	s8 *terms = NULL;
	char *t;
	Dict *dicts = NULL;
	size_t i, ndicts = 0, nterms = 0;
	int iflag = 0;

	argv0 = argv[0];

	ARGBEGIN {
	case 'd':
		t = EARGF(usage());
		for (i = 0; i < LEN(default_dict_map); i++) {
			if (strcmp(t, default_dict_map[i].rom) == 0) {
				dicts = &default_dict_map[i];
				ndicts++;
				break;
			}
		}
		if (dicts == NULL)
			die("invalid dictionary name: %s\n", t);
		break;
	case 'F':
		t = EARGF(usage());
		fsep = unescape((s8){t, strlen(t)});
		break;
	case 'i':
		iflag = 1;
		break;
	default:
		usage();
	} ARGEND

	if (ndicts == 0) {
		dicts = default_dict_map;
		ndicts = LEN(default_dict_map);
	}

	/* remaining argv elements are terms to search for */
	for (i = 0; argc && *argv; argv++, i++, argc--) {
		terms = xreallocarray(terms, ++nterms, sizeof(s8));
		terms[i].s = *argv;
		terms[i].len = strlen(terms[i].s);
	}

	if (nterms == 0 && iflag == 0)
		usage();

	if (iflag == 0)
		for (i = 0; i < ndicts; i++)
			find_and_print_defs(&dicts[i], terms, nterms);
	else
		repl(dicts, ndicts);

	free(terms);

	return 0;
}
