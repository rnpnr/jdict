/* See LICENSE for license details. */
typedef unsigned char  u8;
typedef signed   long  i64;
typedef unsigned long  u64;
typedef signed   int   i32;
typedef unsigned int   u32;
typedef unsigned int   b32;
typedef unsigned short u16;
typedef signed   long  size;
typedef unsigned long  usize;
typedef signed   long  iptr;

#define SYS_read       0
#define SYS_write      1
#define SYS_close      3
#define SYS_fstat      5
#define SYS_mmap       9
#define SYS_exit       60
#define SYS_getdents64 217
#define SYS_openat     257

#define PAGESIZE 4096

#define O_DIRECTORY   0x10000

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
syscall4(i64 n, i64 a1, i64 a2, i64 a3, i64 a4)
{
	i64 result;
	register i64 r10 asm("r10") = a4;
	asm volatile ("syscall"
		: "=a"(result)
		: "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
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
