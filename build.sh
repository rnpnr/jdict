#!/bin/sh
cflags="-march=native -std=c99 -Wall -Wextra -fno-builtin -static"
#cflags="${cflags} -fproc-stat-report"
#cflags="${cflags} -Rpass-missed=.*"
#cflags="${cflags} -fsanitize=address,undefined"

cc=${CC:-cc}
build=release

for arg in "$@"; do
	case "$arg" in
	clang)   cc=clang      ;;
	gcc)     cc=gcc        ;;
	debug)   build=debug   ;;
	release) build=release ;;
	*) echo "usage: $0 [debug|release] [gcc|clang]" ;;
	esac
done

case "${build}" in
debug)   cflags="${cflags} -O0 -ggdb -D_DEBUG" ;;
release) cflags="${cflags} -O3 -s" ;;
esac

src=platform_posix.c

case $(uname -sm) in
"Linux aarch64")
	src=platform_linux_aarch64.c
	cflags="${cflags} -nostdlib -ffreestanding -fno-stack-protector -Wl,--gc-sections"
	;;
"Linux x86_64")
	src=platform_linux_amd64.c
	cflags="${cflags} -nostdinc -nostdlib -ffreestanding -fno-stack-protector -Wl,--gc-sections"
	;;
esac

${cc} ${cflags} ${ldflags} $src -o jdict

# NOTE(rnp): cross compile tests
clang --target=x86_64-unknown-linux-musl  -O3 -nostdlib -ffreestanding -fno-stack-protector \
	-Wl,--gc-sections platform_linux_amd64.c -o /dev/null
clang --target=aarch64-unknown-linux-musl -O3 -nostdlib -ffreestanding -fno-stack-protector \
	-Wl,--gc-sections platform_linux_aarch64.c -o /dev/null
