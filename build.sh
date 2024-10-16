#!/bin/sh
cflags="-march=native -O3 -std=c99 -Wall -Wextra -fno-builtin"
#cflags="${cflags} -fproc-stat-report"
#cflags="${cflags} -Rpass-missed=.*"
ldflags="-static"

cc=${CC:-cc}
debug=${DEBUG}

src=platform_posix.c

[ $debug ] && cflags="$cflags -O0 -ggdb -D_DEBUG"
[ ! $debug ] && ldflags="-s $ldflags"

${cc} $cflags $ldflags $src -o jdict
