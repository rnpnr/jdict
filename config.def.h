/* See LICENSE for license details. */

/* number of threads to use for scanning dictionaries */
#define NTHREADS 4

/* dir where unzipped yomidicts are stored */
static char *prefix = "/usr/share/yomidicts";

/* field separator for output printing */
static char *fsep = "\t";

/* repl prompt and quit strings */
static char *repl_prompt = "\033[32;1m入力:\033[0m ";
static char *repl_quit = "\n\033[36m(=^ᆺ^)ﾉ　バイバイ～\033[0m";

/* default yomidicts to search */
Dict default_dict_map[] = {
	/* folder name       display name */
	{.rom = "daijirin",  .name = "【三省堂　スーパー大辞林】"},
	{.rom = "daijisen",  .name = "【大辞泉】"},
	{.rom = "koujien",   .name = "【広辞苑】"},
};
