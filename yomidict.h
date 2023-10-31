/* See LICENSE for license details. */
typedef enum {
	YOMI_UNDEF = 0,
	YOMI_ENTRY = 1,
	YOMI_ARRAY = 2,
	YOMI_STR = 4,
	YOMI_NUM = 8
} YomiType;

typedef struct {
	unsigned long start;
	unsigned long end;
	unsigned long len;
	int parent; /* parent tok number */
	YomiType type;
} YomiTok;

typedef struct {
	unsigned long pos; /* offset in yomi bank */
	unsigned long toknext;
	int parent; /* parent tok of current element */
} YomiParser;

enum {
	YOMI_ERROR_NOMEM = -1,
	YOMI_ERROR_INVAL = -2,
	YOMI_ERROR_MALFO = -3
};

void yomi_init(YomiParser *);
int yomi_parse(YomiParser *, YomiTok *, unsigned long, const char *, unsigned long);
