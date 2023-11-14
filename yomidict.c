/* See LICENSE for license details.
 *
 * yomidict.c implements a simple lexer for yomichan dictionary
 * text. This is all it knows how to do. Finding and reading term
 * banks as well as searching through lexed tokens should be
 * implemented elsewhere.
 */
#include <stddef.h>
#include <stdlib.h>

#include "util.h"
#include "yomidict.h"

#define ul unsigned long

#define ISDIGIT(c) ((c) >= '0' && (c) <= '9')

struct YomiScanner {
	const char *data;
	ul len;
	ul pos; /* offset in yomi bank */
	ul toknext;
	int parent; /* parent tok of current element */
};

YomiScanner *
yomi_scanner_new(const char *data, unsigned long datalen)
{
	YomiScanner *s = xreallocarray(NULL, sizeof(YomiScanner), 1);
	s->data = data;
	s->len = datalen;
	s->pos = 0;
	s->toknext = 0;
	s->parent = -1;
	return s;
}

static YomiTok *
alloctok(YomiScanner *s, YomiTok *toks, ul ntoks)
{
	YomiTok *t;

	if (ntoks <= s->toknext)
		return NULL;

	t = &toks[s->toknext++];
	t->parent = -1;
	t->start = -1;
	t->end = -1;
	t->len = 0;

	return t;
}

static int
string(YomiScanner *s, YomiTok *t)
{
	const char *d = s->data;
	ul start = s->pos++;

	for (; s->pos < s->len; s->pos++) {
		/* skip over escaped " */
		if (d[s->pos] == '\\' && s->pos + 1 < s->len && d[s->pos + 1] == '\"') {
			s->pos++;
			continue;
		}

		/* end of str */
		if (d[s->pos] == '\"') {
			t->start = start + 1;
			t->end = s->pos;
			t->parent = s->parent;
			t->type = YOMI_STR;
			return 0;
		}
	}

	s->pos = start;
	return YOMI_ERROR_MALFO;
}

static int
number(YomiScanner *s, YomiTok *t)
{
	const char *d = s->data;
	ul start = s->pos;

	for (; s->pos < s->len; s->pos++) {
		switch (d[s->pos]) {
		case ' ':
		case ',':
		case '\n':
		case '\r':
		case '\t':
		case ']':
			t->parent = s->parent;
			t->start = start;
			t->end = s->pos;
			t->type = YOMI_NUM;
			s->pos--;
			return 0;
		}
		if (!ISDIGIT(d[s->pos])) {
			s->pos = start;
			return YOMI_ERROR_INVAL;
		}
	}
	s->pos = start;
	return YOMI_ERROR_MALFO;
}

int
yomi_scan(YomiScanner *s, YomiTok *toks, ul ntoks)
{
	YomiTok *tok;
	int r, count = s->toknext;

	if (toks == NULL)
		return -1;

	for (; s->pos < s->len; s->pos++) {
		switch (s->data[s->pos]) {
		case '[': /* YOMI_ARRAY || YOMI_ENTRY */
			count++;

			tok = alloctok(s, toks, ntoks);
			if (!tok)
				return YOMI_ERROR_NOMEM;

			if (s->parent == -1 || toks[s->parent].type != YOMI_ARRAY) {
				tok->type = YOMI_ARRAY;
			} else {
				tok->type = YOMI_ENTRY;
				toks[s->parent].len++;
			}

			tok->start = s->pos;
			tok->parent = s->parent;
			s->parent = s->toknext - 1; /* the current tok */
			break;

		case ']':
			if (s->toknext < 1 || s->parent == -1)
				return YOMI_ERROR_INVAL;

			tok = &toks[s->parent];
			for (;;) {
				if (tok->start != (ul)-1 && tok->end == (ul)-1) {
					/* inside unfinished tok */
					tok->end = s->pos + 1;
					s->parent = tok->parent;
					break;
				} else if (tok->parent == -1) {
					 /* this is the super tok */
					break;
				} else {
					tok = &toks[tok->parent];
				}
			}
			break;

		case ',':
			if (s->parent != -1 &&
			    toks[s->parent].type != YOMI_ARRAY &&
			    toks[s->parent].type != YOMI_ENTRY)
				s->parent = toks[s->parent].parent;
			break;

		case '\"':
			tok = alloctok(s, toks, ntoks);
			if (tok == NULL)
				return YOMI_ERROR_NOMEM;

			r = string(s, tok);
			if (r != 0)
				return r;

			count++;
			if (s->parent != -1)
				toks[s->parent].len++;
			else
				toks[0].len++;

		case ' ': /* FALLTHROUGH */
		case '\n':
		case '\r':
		case '\t':
			break;

		default:
			tok = alloctok(s, toks, ntoks);
			if (tok == NULL)
				return YOMI_ERROR_NOMEM;

			r = number(s, tok);
			if (r != 0)
				return r;

			count++;
			if (s->parent != -1)
				toks[s->parent].len++;
			else
				toks[0].len++;
		}
	}
	return count;
}
