/* See LICENSE for license details. */
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h> /* for SIZE_MAX */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "arg.h"
#include "util.h"
#include "yomidict.h"

#include "config.h"

#define YOMI_STRIDE_GUESS 10000UL
#define YOMI_TOKS_PER_ENT 10
#define YOMI_TOK_DELTA (YOMI_TOKS_PER_ENT * 100)

/* buffer length for interactive mode */
#define BUFLEN 256

typedef struct {
	char *term;
	char **defs;
	size_t ndefs;
} DictEnt;

struct par_thread_arg {
	DictEnt *ents;
	size_t len, nents;
	const char *pre;
	int start, end;
};

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
}

static int
entcmp(const void *va, const void *vb)
{
	const DictEnt *a = va, *b = vb;
	if (a->term == NULL || b->term == NULL) {
		if (a->term == NULL && b->term)
			return -1;
		else if (a->term && b->term == NULL)
			return 1;
		return 0;
	}
	return strcmp(a->term, b->term);
}

static void
merge_ents(DictEnt *a, DictEnt *b)
{
	size_t i, nlen = a->ndefs + b->ndefs;

	if (nlen == 0)
		return;

	a->defs = xreallocarray(a->defs, nlen, sizeof(char *));

	for (i = 0; i < b->ndefs; i++)
		a->defs[a->ndefs + i] = b->defs[i];
	a->ndefs = nlen;
}

static DictEnt *
dedup(DictEnt *ents, size_t *nents)
{
	size_t i, j, len = 0;
	DictEnt *dents = xreallocarray(NULL, *nents, sizeof(DictEnt));

	for (i = 0; i < *nents - 1; i = j) {
		for (j = i+1; j < *nents && !entcmp(&ents[i], &ents[j]); j++) {
			merge_ents(&ents[i], &ents[j]);
			/* don't leak memory after merging */
			free(ents[j].term);
			free(ents[j].defs);
		}
		memcpy(&dents[len++], &ents[i], sizeof(DictEnt));
	}
	/* move last ent if it wasn't a duplicate */
	if (i + 1 < *nents)
		memcpy(&dents[len++], &ents[i+1], sizeof(DictEnt));

	/* all entries were copied to dents so old ents can be freed.
	 * the term and defs ptrs shouldn't be removed since they still
	 * point to their respective data. the duplicates were freed above
	 */
	free(ents);
	*nents = len;
	return xreallocarray(dents, *nents, sizeof(DictEnt));
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
	d->term = xmemdup(data + tstr->start, tstr->end - tstr->start);
	d->ndefs = tdefs->len;
	d->defs = xreallocarray(NULL, d->ndefs, sizeof(char *));
	for (i = 1; i <= d->ndefs; i++)
		d->defs[i - 1] = xmemdup(data + tdefs[i].start,
		                         tdefs[i].end - tdefs[i].start);

	return d;
}

static size_t
parse_term_bank(DictEnt *ents, size_t len, const char *tbank)
{
	int i = 0, r, ntoks, fd;
	size_t flen, nents = 0;
	char *data;
	YomiTok *toks = NULL;
	YomiScanner *s = NULL;
	DictEnt *e;

	if (len == 0)
		return 0;

	if ((fd = open(tbank, O_RDONLY)) < 0)
		die("can't open file: %s\n", tbank);
	flen = lseek(fd, 0, SEEK_END);
	data = mmap(NULL, flen, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	if (data == MAP_FAILED)
		die("couldn't mmap file: %s\n", tbank);

	/* allocate tokens */
	if ((SIZE_MAX - 1) / YOMI_TOKS_PER_ENT < len)
		die("ntoks multiplication overflowed: %s\n", tbank);
	ntoks = len * YOMI_TOKS_PER_ENT + 1;
	toks = xreallocarray(toks, ntoks, sizeof(YomiTok));

	s = yomi_scanner_new(data, flen);
	while ((r = yomi_scan(s, toks, ntoks)) < 0) {
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

		e = make_ent(&toks[i], data);
		if (e == NULL)
			break;
		memcpy(&ents[nents++], e, sizeof(DictEnt));
	}

cleanup:
	munmap(data, flen);
	free(toks);
	free(s);

	return nents;
}

static void *
parser_thread(void *arg)
{
	struct par_thread_arg *a = arg;
	char tbank[PATH_MAX];
	int i;

	a->nents = 0;
	for (i = a->start; i <= a->end; i++) {
		size_t rem = a->len - a->nents;
		snprintf(tbank, LEN(tbank), "%s/term_bank_%d.json", a->pre, i);
		a->nents += parse_term_bank(&a->ents[a->nents], rem, tbank);
	}

	/* ensure extra memory is in a predictable state */
	memset(&a->ents[a->nents], 0, (a->len - a->nents) * sizeof(DictEnt));

	return NULL;
}

static size_t
parallel_parse_term_banks(DictEnt *ents, size_t len, const char *pre,
                          size_t nbanks, size_t bank_start)
{
	pthread_t th[NTHREADS];
	struct par_thread_arg args[NTHREADS];
	size_t nents = 0;
	size_t split = (nbanks - bank_start + 1) / NTHREADS;
	size_t extras = (nbanks - bank_start + 1) % NTHREADS;
	int i;

	/* spawn threads */
	for (i = 0; i < NTHREADS; i++) {
		args[i].pre = pre;
		args[i].len = len/NTHREADS;
		args[i].ents = &ents[i * len/NTHREADS];
		args[i].start = i? args[i-1].end + 1 : bank_start;
		args[i].end = args[i].start + split - 1;

		/* evenly distribute remaining banks */
		if (extras) {
			extras--;
			args[i].end++;
		}

		pthread_create(&th[i], 0, parser_thread, &args[i]);
	}

	/* wait for threads to exit and count entries */
	for (i = 0; i < NTHREADS; i++) {
		pthread_join(th[i], NULL);
		nents += args[i].nents;
	}
	return nents;
}

static DictEnt *
make_dict(struct Dict *dict, size_t *nlen)
{
	char path[PATH_MAX - 20], tbank[PATH_MAX];
	size_t nbanks, nents = 0, lents = 0;
	DictEnt *ents = NULL;

	snprintf(path, LEN(path), "%s/%s", prefix, dict->rom);

	if ((nbanks = count_term_banks(path)) == 0)  {
		fprintf(stderr, "no term banks found: %s\n", path);
		return NULL;
	}

	/* parse first bank to get a guess for the total number of entries */
	snprintf(tbank, LEN(tbank), "%s/term_bank_%d.json", path, 1);
	do {
		lents += YOMI_STRIDE_GUESS;
		ents = xreallocarray(ents, lents, sizeof(DictEnt));
		nents = parse_term_bank(ents, lents, tbank);
	} while (nents == 0);

	/* alloc enough memory for all ents */
	if (SIZE_MAX / nbanks < nents)
		die("dict has too many entries: %s\n", dict->rom);
	lents = nents * nbanks;
	ents = xreallocarray(ents, lents, sizeof(DictEnt));

	nents += parallel_parse_term_banks(&ents[nents], lents - nents,
	                                   path, nbanks, 2);

	qsort(ents, nents, sizeof(DictEnt), entcmp);
	ents = dedup(ents, &nents);
	*nlen = nents;

	return ents;
}

static DictEnt **
make_dicts(struct Dict *dicts, size_t ndicts, size_t *nents)
{
	DictEnt **ents;
	size_t i;

	ents = xreallocarray(NULL, ndicts, sizeof(DictEnt *));

	for (i = 0; i < ndicts; i++) {
		ents[i] = make_dict(&dicts[i], &nents[i]);
		if (ents[i] == NULL)
			die("make_dict(%s): returned NULL\n", dicts[i].rom);
	}

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

static void
find_and_print_defs(struct Dict *dict, char **terms, size_t nterms)
{
	size_t i, nents = 0;
	DictEnt *ents;

	ents = *make_dicts(dict, 1, &nents);
	if (!ents) {
		fputs("failed to allocate dict: ", stdout);
		puts(dict->rom);
		return;
	}

	puts(dict->name);
	for (i = 0; i < nterms; i++)
		find_and_print(terms[i], ents, nents);

	free_ents(ents, nents);
}

static void
repl(struct Dict *dicts, size_t ndicts)
{
	DictEnt **ents;
	char buf[BUFLEN];
	size_t i, *nents;

	nents = xreallocarray(NULL, ndicts, sizeof(size_t));
	ents = make_dicts(dicts, ndicts, nents);

	for (;;) {
		fputs(repl_prompt, stdout);
		fflush(stdout);
		if (fgets(buf, LEN(buf), stdin) == NULL)
			break;
		for (i = 0; i < ndicts; i++) {
			puts(dicts[i].name);
			find_and_print(trim(buf), ents[i], nents[i]);
		}
	}
	puts(repl_quit);

	for (i = 0; i < ndicts; i++)
		free_ents(ents[i], nents[i]);
	free(ents);
	free(nents);
}

int
main(int argc, char *argv[])
{
	char **terms = NULL, *t;
	struct Dict *dicts = NULL;
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
