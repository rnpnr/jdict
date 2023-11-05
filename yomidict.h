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

typedef struct YomiScanner YomiScanner;

enum {
	YOMI_ERROR_NOMEM = -1,
	YOMI_ERROR_INVAL = -2,
	YOMI_ERROR_MALFO = -3
};

YomiScanner *yomi_scanner_new(const char *, unsigned long);
int yomi_scan(YomiScanner *, YomiTok *, unsigned long);
