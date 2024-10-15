/* See LICENSE for license details. */

/* dir where unzipped yomidicts are stored */
static s8 prefix = s8("/usr/share/yomidicts");

/* field separator for output printing */
static s8 fsep = s8("\t");

/* repl prompt and quit strings */
static s8 repl_prompt = s8("\033[32;1m入力:\033[0m ");
static s8 repl_quit   = s8("\n\033[36m(=^ᆺ^)ﾉ　バイバイ～\033[0m");

/* default yomidicts to search */
Dict default_dict_map[] = {
	/* folder name           display name */
	{.rom = s8("daijirin"),  .name = s8("【三省堂　スーパー大辞林】")},
	{.rom = s8("daijisen"),  .name = s8("【大辞泉】")},
	{.rom = s8("koujien"),   .name = s8("【広辞苑】")},
};
