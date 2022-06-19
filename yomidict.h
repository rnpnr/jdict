/* See LICENSE for license details. */
typedef enum {
	YOMI_UNDEF = 0,
	YOMI_ENTRY = 1,
	YOMI_ARRAY = 2,
	YOMI_STR = 4,
	YOMI_NUM = 8
} YomiType;

typedef struct {
	size_t start;
	size_t end;
	size_t len;
	int parent; /* parent tok number */
	YomiType type;
} YomiTok;

typedef struct {
	size_t pos; /* offset in yomi bank */
	size_t toknext;
	int parent; /* parent tok of current element */
} YomiParser;

enum {
	YOMI_ERROR_NOMEM = -1,
	YOMI_ERROR_INVAL = -2,
	YOMI_ERROR_MALFO = -3
};

void yomi_init(YomiParser *);
int yomi_parse(YomiParser *, YomiTok *, size_t, const char *, size_t);
