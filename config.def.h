/* See LICENSE for license details. */

/* dir where unzipped yomidicts are stored */
static char *prefix = "/usr/share/yomidicts";

/* repl prompt and quit strings */
static char *repl_prompt = "\033[32;1m入力:\033[0m ";
static char *repl_quit = "\n\033[36m(=^ᆺ^)ﾉ　バイバイ～\033[0m";

/* default yomidicts to search */
static struct Dict {
	const char *rom;
	const char *name;
} default_dict_map[] = {
	/* folder name      display name */
	{"daijirin",      "【三省堂　スーパー大辞林】"},
	{"daijisen",      "【大辞泉】"},
	{"koujien",       "【広辞苑】"},
};
