#!/bin/sh
cflags="-march=native -O3 -std=c99 -Wall -Wextra"
cflags="${cflags} -D_DEFAULT_SOURCE"
#cflags="${cflags} -fproc-stat-report"
#cflags="${cflags} -Rpass-missed=.*"
ldflags="-static"

cc=${CC:-cc}
debug=${DEBUG}

[ $debug ] && cflags="$cflags -O0 -ggdb -D_DEBUG"
[ ! $debug ] && ldflags="-s $ldflags"

${cc} $cflags $ldflags jdict.c -o jdict
