/* See LICENSE for license details. */
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "arg.h"
#include "util.h"
#include "yomidict.h"

#include "config.h"

#define YOMI_TOKS_PER_ENT 10
#define YOMI_TOK_DELTA (YOMI_TOKS_PER_ENT * 100)

typedef struct {
	char *term;
	char **defs;
	size_t ndefs;
} DictEnt;

char *argv0;

static void
cleanup(char **terms)
{
	free(terms);
	terms = NULL;
}

static void
usage(void)
{
	die("usage: %s [-d path] term ...\n", argv0);
}

/* takes a token of type YOMI_ENTRY and creates a DictEnt */
static DictEnt *
make_ent(YomiTok *toks, size_t ntoks, char *data)
{
	size_t i;
	DictEnt *d;
	YomiTok *tstr, *tdefs;

	if (toks[0].type != YOMI_ENTRY)
		return NULL;

	/* FIXME: hacky but works */
	/* definition array = YOMI_ENTRY tok + 6 */
	if (ntoks - 6 < 0)
		return NULL;
	tdefs = toks + 6;
	/* term = YOMI_ENTRY tok + 1 */
	tstr = toks + 1;

	d = xreallocarray(NULL, 1, sizeof(DictEnt));
	d->term = strndup(data + tstr->start, tstr->end - tstr->start);
	d->ndefs = tdefs->len;
	d->defs = xreallocarray(NULL, d->ndefs, sizeof(char *));
	for (i = 1; i <= d->ndefs; i++)
		d->defs[i-1] = strndup(data + (tdefs + i)->start,
		                       (tdefs + i)->end - (tdefs + i)->start);

	return d;
}

static DictEnt *
parse_term_bank(DictEnt *ents, size_t *nents, const char *tbank, size_t *stride)
{
	int r, ntoks, fd;
	size_t i, flen;
	char *data;
	YomiTok *toks = NULL;
	YomiParser p;
	DictEnt *e;

	if ((fd = open(tbank, O_RDONLY)) < 0)
		die("can't open file: %s\n", tbank);
	flen = lseek(fd, 0, SEEK_END);
	data = mmap(NULL, flen, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	if (data == MAP_FAILED)
		die("couldn't mmap file: %s\n", tbank);

	/* allocate tokens */
	ntoks = *stride * YOMI_TOKS_PER_ENT + 1;
	if ((ntoks - 1) / YOMI_TOKS_PER_ENT != *stride)
		die("stride multiplication overflowed: %s\n", tbank);
	toks = xreallocarray(toks, ntoks, sizeof(YomiTok));

	yomi_init(&p);
	while ((r = yomi_parse(&p, toks, ntoks, data, flen)) < 0) {
		switch (r) {
		case YOMI_ERROR_NOMEM:
			/* allocate more mem and try again */
			if (ntoks + YOMI_TOK_DELTA < 0)
				die("too many toks: %s\n", tbank);
			ntoks += YOMI_TOK_DELTA;
			toks = xreallocarray(toks, ntoks, sizeof(YomiTok));
			*stride = ntoks/YOMI_TOKS_PER_ENT;
			break;
		case YOMI_ERROR_INVAL: /* FALLTHROUGH */
		case YOMI_ERROR_MALFO:
			munmap(data, flen);
			free(toks);
			return NULL;
		}
	}

	ents = xreallocarray(ents, (*nents) + r/YOMI_TOKS_PER_ENT, sizeof(DictEnt));
	for (i = 0; i < r; i++) {
		if (toks[i].type == YOMI_ENTRY) {
			e = make_ent(&toks[i], r - i, data);
			if (e == NULL)
				return NULL;
			memcpy(&ents[(*nents)++], e, sizeof(DictEnt));
		}
	}
	munmap(data, flen);
	free(toks);

	return ents;
}

static DictEnt *
make_dict(const char *path, size_t *stride, size_t *nents)
{
	char tbank[PATH_MAX];
	size_t i, nbanks = 0;
	DIR *dir;
	struct dirent *dent;
	DictEnt *dict = NULL;

	if (!(dir = opendir(path)))
		die("opendir(): failed to open: %s\n", path);

	/* count term banks in path */
	while ((dent = readdir(dir)) != NULL)
		if (dent->d_type == DT_REG)
			nbanks++;
	/* remove index.json from count */
	nbanks--;

	closedir(dir);

	for (i = 1; i <= nbanks; i++) {
		snprintf(tbank, sizeof(tbank), "%s/term_bank_%d.json", path, (int)i);
		dict = parse_term_bank(dict, nents, tbank, stride);
		if (dict == NULL)
			return NULL;
	}

	return dict;
}

static int
entcmp(const void *va, const void *vb)
{
	const DictEnt *a = va, *b = vb;
	return strcmp(a->term, b->term);
}


static DictEnt *
find_ent(const char *term, DictEnt *ents, size_t nents)
{
	int r;

	if (nents == 0)
		return NULL;

	r = strcmp(term, ents[nents/2].term);
	if (r == 0)
		return &ents[nents/2];
	if (r < 0)
		return find_ent(term, ents, nents/2);

	if (nents % 2)
		return find_ent(term, &ents[nents/2 + 1], nents/2);

	return find_ent(term, &ents[nents/2 + 1], nents/2 - 1);
}

static char *
fix_newlines(char *str)
{
	char *t = str;

	while ((t = strstr(t, "\\n")) != NULL) {
		t[0] = '\n';
		t++;
		memmove(t, t + 1, strlen(t + 1) + 1);
	}

	return str;
}

static void
print_ent(DictEnt *ent)
{
	size_t i;
	for (i = 0; i < ent->ndefs; i++)
		printf("%s\n", fix_newlines(ent->defs[i]));
}

static int
find_and_print_defs(struct Dict *dict, char **terms, size_t nterms)
{
	char path[PATH_MAX - 18];
	size_t i, j;
	size_t nents = 0;
	DictEnt *ent, *ents;

	snprintf(path, LEN(path), "%s/%s", prefix, dict->rom);
	ents = make_dict(path, &dict->stride, &nents);
	if (ents == NULL)
		return -1;
	qsort(ents, nents, sizeof(DictEnt), entcmp);
		printf("%s\n", dict->name);

	for (i = 0; i < nterms; i++) {
		ent = find_ent(terms[i], ents, nents);
		if (ent == NULL) {
			printf("term not found: %s\n\n", terms[i]);
			continue;
		}
		print_ent(ent);
	}
	for (i = 0; i < nents; i++) {
		for (j = 0; j < ents[i].ndefs; j++)
			free(ents[i].defs[j]);
		free(ents[i].defs);
		free(ents[i].term);
	}
	free(ents);

	return 0;
}

int
main(int argc, char *argv[])
{
	char **terms = NULL, *t;
	struct Dict *dicts = NULL;
	size_t ndicts = 0, nterms = 0;
	int i;

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
	default:
		usage();
	} ARGEND

	if (ndicts == 0) {
		dicts = default_dict_map;
		ndicts = LEN(default_dict_map);
	}

	/* remaining argv elements are terms to search for */
	for (i = 0; argc && *argv; argv++, i++, argc--) {
		terms = xreallocarray(terms, ++nterms, sizeof(char *));
		terms[i] = *argv;
	}

	if (nterms == 0) {
		cleanup(terms);
		usage();
	}

	for (i = 0; i < ndicts; i++)
		find_and_print_defs(&dicts[i], terms, nterms);

	cleanup(terms);

	return 0;
}
