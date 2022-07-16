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

/* buffer length for interactive mode */
#define BUFLEN 256

typedef struct {
	char *term;
	char **defs;
	size_t ndefs;
} DictEnt;

char *argv0;

static void
usage(void)
{
	die("usage: %s [-d path] [-i] term ...\n", argv0);
}

static void
free_ents(DictEnt *ents, size_t nents)
{
	size_t i, j;

	for (i = 0; i < nents; i++) {
		for (j = 0; j < ents[i].ndefs; j++)
			free(ents[i].defs[j]);
		free(ents[i].defs);
		free(ents[i].term);
	}
	free(ents);
	ents = NULL;
}

static int
entcmp(const void *va, const void *vb)
{
	const DictEnt *a = va, *b = vb;
	return strcmp(a->term, b->term);
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
			free(ents);
			ents = NULL;
			goto cleanup;
		}
	}

	ents = xreallocarray(ents, (*nents) + r/YOMI_TOKS_PER_ENT, sizeof(DictEnt));
	for (i = 0; i < r; i++) {
		if (toks[i].type != YOMI_ENTRY)
			continue;

		e = make_ent(&toks[i], r - i, data);
		if (e != NULL) {
			memcpy(&ents[(*nents)++], e, sizeof(DictEnt));
		} else {
			free(ents);
			ents = NULL;
			break;
		}
	}

cleanup:
	munmap(data, flen);
	free(toks);

	return ents;
}

static DictEnt *
make_dict(struct Dict *dict, size_t *nents)
{
	char path[PATH_MAX - 20], tbank[PATH_MAX];
	size_t i, nbanks = 0;
	DIR *dir;
	struct dirent *dent;
	DictEnt *ents = NULL;

	snprintf(path, LEN(path), "%s/%s", prefix, dict->rom);
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
		snprintf(tbank, LEN(tbank), "%s/term_bank_%d.json", path, (int)i);
		ents = parse_term_bank(ents, nents, tbank, &dict->stride);
		if (ents == NULL)
			return NULL;
	}
	qsort(ents, *nents, sizeof(DictEnt), entcmp);

	return ents;
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

static void
print_ent(DictEnt *ent)
{
	size_t i;
	for (i = 0; i < ent->ndefs; i++)
		puts(fix_newlines(ent->defs[i]));
}

static void
find_and_print(const char *term, DictEnt *ents, size_t nents)
{
	DictEnt *ent;

	ent = find_ent(term, ents, nents);
	if (ent)
		print_ent(ent);
	else
		printf("term not found: %s\n\n", term);
}

static int
find_and_print_defs(struct Dict *dict, char **terms, size_t nterms)
{
	size_t i, nents = 0;
	DictEnt *ents;

	ents = make_dict(dict, &nents);
	if (ents == NULL)
		return -1;

	puts(dict->name);
	for (i = 0; i < nterms; i++)
		find_and_print(terms[i], ents, nents);

	free_ents(ents, nents);

	return 0;
}

static int
repl(struct Dict *dicts, size_t ndicts)
{
	DictEnt **ents;
	char buf[BUFLEN];
	size_t i, *nents;

	nents = xreallocarray(NULL, ndicts, sizeof(size_t));
	ents = xreallocarray(NULL, ndicts, sizeof(DictEnt *));

	for (i = 0; i < ndicts; i++) {
		ents[i] = make_dict(&dicts[i], &nents[i]);
		if (ents[i] == NULL)
			die("make_dict(%s): returned NULL\n", dicts[i].rom);
	}

	fputs(repl_prompt, stdout);
	fflush(stdout);
	while (fgets(buf, LEN(buf), stdin)) {
		trim(buf);
		for (i = 0; i < ndicts; i++) {
			puts(dicts[i].name);
			find_and_print(buf, ents[i], nents[i]);
		}
		fputs(repl_prompt, stdout);
		fflush(stdout);
	}
	puts(repl_quit);

	for (i = 0; i < ndicts; i++)
		free_ents(ents[i], nents[i]);
	free(ents);
	free(nents);

	return 0;
}

int
main(int argc, char *argv[])
{
	char **terms = NULL, *t;
	struct Dict *dicts = NULL;
	size_t ndicts = 0, nterms = 0;
	int i, iflag = 0;

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
		terms = xreallocarray(terms, ++nterms, sizeof(char *));
		terms[i] = *argv;
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
