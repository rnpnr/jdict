/* See LICENSE for license details. */

/* dir where unzipped yomidicts are stored */
static char *prefix = "/usr/share/yomidicts";

/* default yomidicts to search */
static struct Dict {
	const char *rom;
	const char *name;
	size_t stride;
} default_dict_map[] = {
	{"daijirin", "【三省堂　スーパー大辞林】", 10000},
	{"daijisen", "【大辞泉】", 10000},
	{"koujien", "【広辞苑】", 10000},
};
