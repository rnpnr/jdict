# See LICENSE for license details.
PREFIX = /usr/local

CPPFLAGS = -D_BSD_SOURCE
CFLAGS = -O3 -std=c99 -Wall -Wextra -pedantic -pthread
LDFLAGS = -s -static
