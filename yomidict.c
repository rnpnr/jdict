/* See LICENSE for license details.
 *
 * yomidict.c implements a simple parser for yomichan dictionary text. This is
 * all it knows how to do. Finding and reading term banks as well as searching
 * through parsed entries should be implemented elsewhere.
 */
#include <ctype.h>
#include <stddef.h>

#include "yomidict.h"

void
yomi_init(YomiParser *p)
{
	p->pos = 0;
	p->toknext = 0;
	p->parent = -1;
}

static YomiTok *
yomi_alloc_tok(YomiParser *p, YomiTok *toks, size_t ntoks)
{
	YomiTok *t;

	if (ntoks <= p->toknext)
		return NULL;

	t = &toks[p->toknext++];
	t->parent = -1;
	t->start = -1;
	t->end = -1;
	t->len = 0;

	return t;
}

static int
yomi_parse_str(YomiParser *p, YomiTok *t, const char *s, size_t slen)
{
	size_t i, start = p->pos;
	int c;

	/* skip leading quote */
	p->pos++;

	for (; p->pos < slen && s[p->pos]; p->pos++) {
		c = s[p->pos];

		/* end of str */
		if (c == '\"') {
			t->start = start + 1;
			t->end = p->pos;
			t->parent = p->parent;
			t->type = YOMI_STR;
			return 0;
		}

		/* handle escape chars */
		if (c == '\\' && p->pos + 1 < slen) {
			p->pos++;
			switch (s[p->pos]) {
			case '/': /* FALLTHROUGH */
			case '\"':
			case '\\':
			case 'b':
			case 'f':
			case 'n':
			case 'r':
			case 't':
				break;
			case 'u': /* unicode symbol */
				p->pos++;
				for (i = 0; i < 4 && p->pos < slen && s[p->pos]; i++) {
					if (!isxdigit(s[p->pos])) {
						p->pos = start;
						return YOMI_ERROR_INVAL;
					}
					p->pos++;
				}
				p->pos--;
				break;
			default:
				p->pos = start;
				return YOMI_ERROR_INVAL;
			}
		}
	}
	p->pos = start;
	return YOMI_ERROR_MALFO;
}

static int
yomi_parse_num(YomiParser *p, YomiTok *t, const char *s, size_t slen)
{
	size_t start = p->pos;

	for (; p->pos < slen && s[p->pos]; p->pos++) {
		switch (s[p->pos]) {
		case ' ':
		case ',':
		case '\n':
		case '\r':
		case '\t':
		case ']':
			t->parent = p->parent;
			t->start = start;
			t->end = p->pos;
			t->type = YOMI_NUM;
			p->pos--;
			return 0;
		}
		if (!isdigit(s[p->pos])) {
			p->pos = start;
			return YOMI_ERROR_INVAL;
		}
	}
	p->pos = start;
	return YOMI_ERROR_MALFO;
}

int
yomi_parse(YomiParser *p, YomiTok *toks, size_t ntoks,
    const char *bank, size_t blen)
{
	YomiTok *tok, *t;
	int r, count = p->toknext;

	if (toks == NULL)
		return -1;

	for (; p->pos < blen && bank[p->pos]; p->pos++) {
		switch (bank[p->pos]) {
		case '[': /* YOMI_ARRAY || YOMI_ENTRY */
			count++;

			tok = yomi_alloc_tok(p, toks, ntoks);
			if (!tok)
				return YOMI_ERROR_NOMEM;

			t = NULL;
			if (p->parent != -1) {
				t = &toks[p->parent];
				t->len++;
			}

			if (t && t->type == YOMI_ARRAY)
				tok->type = YOMI_ENTRY;
			else
				tok->type = YOMI_ARRAY;

			tok->start = p->pos;
			tok->parent = p->parent;
			p->parent = p->toknext - 1; /* the current tok */
			break;

		case ']':
			if (p->toknext < 1 || p->parent == -1)
				return YOMI_ERROR_INVAL;

			tok = &toks[p->parent];
			for (;;) {
				if (tok->start != -1 && tok->end == -1) {
					/* inside unfinished tok */
					tok->end = p->pos + 1;
					p->parent = tok->parent;
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
			if (p->parent != -1 &&
			    toks[p->parent].type != YOMI_ARRAY &&
			    toks[p->parent].type != YOMI_ENTRY)
				p->parent = toks[p->parent].parent;
			break;

		case '\"':
			tok = yomi_alloc_tok(p, toks, ntoks);
			if (tok == NULL)
				return YOMI_ERROR_NOMEM;

			r = yomi_parse_str(p, tok, bank, blen);
			if (r != 0)
				return r;

			count++;
			if (p->parent != -1)
				toks[p->parent].len++;
			else
				toks[0].len++;

		case ' ': /* FALLTHROUGH */
		case '\n':
		case '\r':
		case '\t':
			break;

		default:
			tok = yomi_alloc_tok(p, toks, ntoks);
			if (tok == NULL)
				return YOMI_ERROR_NOMEM;

			r = yomi_parse_num(p, tok, bank, blen);
			if (r != 0)
				return r;

			count++;
			if (p->parent != -1)
				toks[p->parent].len++;
			else
				toks[0].len++;
		}
	}
	return count;
}
