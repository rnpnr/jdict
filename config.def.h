/* See LICENSE for license details. */

/* max terms per term bank, all dicts should use this stride */
#define DICT_STRIDE 10000

/* dir where unzipped yomidicts are stored */
static char *prefix = "/usr/share/yomidicts";

/* default yomidicts to search */
static char *default_dicts[] = {
	"daijirin"
	"daijisen",
	"koujien"
};
