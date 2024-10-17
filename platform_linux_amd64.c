/* See LICENSE for license details. */

#ifndef asm
#ifdef __asm
#define asm __asm
#else
#define asm __asm__
#endif
#endif

#define SYS_read     0
#define SYS_write    1
#define SYS_open     2
#define SYS_close    3
#define SYS_stat     4
#define SYS_mmap     9
#define SYS_exit     60
#define SYS_getdents 78

#define PAGESIZE 4096

#define STAT_BUF_SIZE 144
#define STAT_SIZE_OFF 48

#define DIRENT_RECLEN_OFF 16
#define DIRENT_NAME_OFF   18

#include "platform_linux.c"

static i64
syscall1(i64 n, i64 a1)
{
	i64 result;
	asm volatile ("syscall"
		: "=a"(result)
		: "a"(n), "D"(a1)
		: "rcx", "r11", "memory"
	);
	return result;
}

static i64
syscall2(i64 n, i64 a1, i64 a2)
{
	i64 result;
	asm volatile ("syscall"
		: "=a"(result)
		: "a"(n), "D"(a1), "S"(a2)
		: "rcx", "r11", "memory"
	);
	return result;
}

static i64
syscall3(i64 n, i64 a1, i64 a2, i64 a3)
{
	i64 result;
	asm volatile ("syscall"
		: "=a"(result)
		: "a"(n), "D"(a1), "S"(a2), "d"(a3)
		: "rcx", "r11", "memory"
	);
	return result;
}

static i64
syscall6(i64 n, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5, i64 a6)
{
	i64 result;
	register i64 r10 asm("r10") = a4;
	register i64 r8  asm("r8")  = a5;
	register i64 r9  asm("r9")  = a6;
	asm volatile ("syscall"
		: "=a"(result)
		: "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
		: "rcx", "r11", "memory"
	);
	return result;
}

asm (
	".intel_syntax noprefix\n"
	".global _start\n"
	"_start:\n"
	"	mov	edi, DWORD PTR [rsp]\n"
	"	lea	rsi, [rsp+8]\n"
	"	lea	rdx, [rsi+rdi*8+8]\n"
	"	call	linux_main\n"
	"	ud2\n"
	".att_syntax\n"
);
