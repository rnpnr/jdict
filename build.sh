#!/bin/sh
set -x

cflags="-march=native -O3 -std=c99 -Wall -Wextra -pedantic"
cflags="$cflags -D_BSD_SOURCE"
ldflags="-s -static"

cc $cflags $ldflags jdict.c -o jdict
