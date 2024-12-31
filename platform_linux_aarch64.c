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

#define SYS_openat             56
#define SYS_close              57
#define SYS_getdents64         61
#define SYS_read               63
#define SYS_write              64
#define SYS_fstat              80
#define SYS_exit               93
#define SYS_mmap              222

/* NOTE(rnp): technically arm64 can have 4K, 16K or 64K pages but we will just assume 64K */
#define PAGESIZE 65536

#define O_DIRECTORY   0x4000

#include "platform_linux.c"

static FORCE_INLINE i64
syscall1(i64 n, i64 a1)
{
	register i64 x8 asm("x8") = n;
	register i64 x0 asm("x0") = a1;
	asm volatile ("svc 0"
		: "=r"(x0)
		: "0"(x0), "r"(x8)
		: "memory", "cc"
	);
	return x0;
}

static FORCE_INLINE i64
syscall2(i64 n, i64 a1, i64 a2)
{
	register i64 x8 asm("x8") = n;
	register i64 x0 asm("x0") = a1;
	register i64 x1 asm("x1") = a2;
	asm volatile ("svc 0"
		: "=r"(x0)
		: "0"(x0), "r"(x8), "r"(x1)
		: "memory", "cc"
	);
	return x0;
}

static FORCE_INLINE i64
syscall3(i64 n, i64 a1, i64 a2, i64 a3)
{
	register i64 x8 asm("x8") = n;
	register i64 x0 asm("x0") = a1;
	register i64 x1 asm("x1") = a2;
	register i64 x2 asm("x2") = a3;
	asm volatile ("svc 0"
		: "=r"(x0)
		: "0"(x0), "r"(x8), "r"(x1), "r"(x2)
		: "memory", "cc"
	);
	return x0;
}

static FORCE_INLINE i64
syscall4(i64 n, i64 a1, i64 a2, i64 a3, i64 a4)
{
	register i64 x8 asm("x8") = n;
	register i64 x0 asm("x0") = a1;
	register i64 x1 asm("x1") = a2;
	register i64 x2 asm("x2") = a3;
	register i64 x3 asm("x3") = a4;
	asm volatile ("svc 0"
		: "=r"(x0)
		: "0"(x0), "r"(x8), "r"(x1), "r"(x2), "r"(x3)
		: "memory", "cc"
	);
	return x0;
}

static FORCE_INLINE i64
syscall6(i64 n, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5, i64 a6)
{
	register i64 x8 asm("x8") = n;
	register i64 x0 asm("x0") = a1;
	register i64 x1 asm("x1") = a2;
	register i64 x2 asm("x2") = a3;
	register i64 x3 asm("x3") = a4;
	register i64 x4 asm("x4") = a5;
	register i64 x5 asm("x5") = a6;
	asm volatile ("svc 0"
		: "=r"(x0)
		: "0"(x0), "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
		: "memory", "cc"
	);
	return x0;
}

asm (
	".global _start\n"
	"_start:\n"
	"	ldr	x0, [sp], #8\n"
	"	mov	x1, sp\n"
	"       add     x2, sp, x0, lsl #3\n"
	"       add     x2, x2, #8\n"
	"       sub     sp, sp, #8\n"
	"	bl      linux_main\n"
	"	brk     #0\n"
);
