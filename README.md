# jdict
A command line lookup tool similar to the browser extension
[Yomichan](https://github.com/FooSoft/yomichan).

## Example Output
	$ jdict 百戦錬磨
	【三省堂　スーパー大辞林】
	ひゃくせん-れんま [5] 【百戦練磨・百戦錬磨】
	多くの戦いできたえられること。多くの経験を積んでいること。「―の勇士」
	
	【大辞泉】
	ひゃくせん‐れんま【百戦錬磨】
	たびたびの戦いで鍛えられていること。また、経験が豊かで処理能力にすぐれていること。「―のつわもの」
	
	【広辞苑】
	ひゃくせん‐れんま【百戦錬磨】
	かずかずの実戦や経験を積んできたえられていること。「―の強者つわもの」
	⇀ひゃく‐せん【百戦】

## Installation

This tool reads dictionaries created by
[yomichan-import](https://github.com/FooSoft/yomichan-import/). The
zip file created by `yomichan-import` needs to be extracted and stored
in the prefix specified in `config.h`. The folder name should be also
specified in `config.h` in addition to the stride parameter used to
generate the dictionary.

After modifying `config.h`, `config.mk` can also be modified to suit
your system and then the following can be used to install (using root
as needed):

	make clean install

