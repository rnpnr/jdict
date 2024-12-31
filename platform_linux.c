/* See LICENSE for license details. */
#define os_path_sep s8("/")

#define NULL ((void *)0)

#include "jdict.c"

#define PROT_READ     0x01
#define PROT_WRITE    0x02
#define PROT_RW       0x03
#define MAP_PRIVATE   0x02
#define MAP_ANON      0x20

#define AT_FDCWD      (-100)

#define O_RDONLY      0x00

#define DT_REGULAR_FILE 8

typedef __attribute__((aligned(16))) u8 stat_buffer[144];
#define STAT_BUF_MEMBER(sb, t, off) (*(t *)((u8 *)(sb) + off))
#define STAT_FILE_SIZE(sb)  STAT_BUF_MEMBER(sb, u64,  48)

#define DIRENT_BUF_MEMBER(db, t, off) (*(t *)((u8 *)(db) + off))
#define DIRENT_RECLEN(db) DIRENT_BUF_MEMBER(db, u16,    16)
#define DIRENT_TYPE(db)   DIRENT_BUF_MEMBER(db, u8,     18)
#define DIRENT_NAME(db)   (char *)((db) + 19)

static i64 syscall1(i64, i64);
static i64 syscall2(i64, i64, i64);
static i64 syscall3(i64, i64, i64, i64);
static i64 syscall4(i64, i64, i64, i64, i64);
static i64 syscall6(i64, i64, i64, i64, i64, i64, i64);

typedef struct {
	u8   buf[2048];
	iptr fd;
	i32  buf_pos;
	i32  buf_end;
} LinuxDirectoryStream;

/* NOTE: necessary garbage required by GCC/CLANG even when -nostdlib is used */
__attribute((section(".text.memset")))
void *memset(void *d, int c, usize n)
{
	return mem_clear(d, c, n);
}

static void
os_exit(i32 code)
{
	syscall1(SYS_exit, code);
	unreachable();
}

static b32
os_write(iptr fd, s8 raw)
{
	while (raw.len) {
		size r = syscall3(SYS_write, fd, (i64)raw.s, raw.len);
		if (r < 0) return 0;
		raw = s8_cut_head(raw, r);
	}
	return 1;
}

static b32
os_read_stdin(u8 *buf, size count)
{
	size rlen = syscall3(SYS_read, 0, (iptr)buf, count);
	return rlen == count;
}

static s8
os_read_whole_file_at(char *file, iptr dir_fd, Arena *a, u32 arena_flags)
{
	u64 fd = syscall4(SYS_openat, dir_fd, (iptr)file, O_RDONLY, 0);
	if (fd > -4096UL) {
		stream_append_s8(&error_stream, s8("failed to open: "));
		stream_append_s8(&error_stream, cstr_to_s8(file));
		die(&error_stream);
	}

	stat_buffer sb;
	u64 status = syscall2(SYS_fstat, fd, (iptr)sb);
	if (status > -4096UL) {
		stream_append_s8(&error_stream, s8("failed to stat: "));
		stream_append_s8(&error_stream, cstr_to_s8(file));
		die(&error_stream);
	}

	u64 file_size = STAT_FILE_SIZE(sb);
	s8 result = {.len = file_size, .s = alloc(a, u8, file_size, arena_flags|ARENA_NO_CLEAR)};
	size rlen = syscall3(SYS_read, fd, (iptr)result.s, result.len);
	syscall1(SYS_close, fd);

	if (rlen != result.len) {
		stream_append_s8(&error_stream, s8("failed to read whole file: "));
		stream_append_s8(&error_stream, cstr_to_s8(file));
		die(&error_stream);
	}

	return result;
}

static Arena
os_new_arena(size requested_size)
{
	Arena result = {0};

	size alloc_size = requested_size;
	if (alloc_size % PAGESIZE != 0)
		alloc_size += PAGESIZE - alloc_size % PAGESIZE;

	u64 memory = syscall6(SYS_mmap, 0, alloc_size, PROT_RW, MAP_ANON|MAP_PRIVATE, -1, 0);
	if (memory <= -4096UL) {
		result.beg = (void *)memory;
		result.end = result.beg + alloc_size;
#ifdef _DEBUG_ARENA
		result.min_capacity_remaining = alloc_size;
#endif
	}

	return result;
}

static iptr
os_begin_path_stream(Stream *dir_name, Arena *a, u32 arena_flags)
{
	stream_append_byte(dir_name, 0);
	u64 fd = syscall4(SYS_openat, AT_FDCWD, (iptr)dir_name->data, O_DIRECTORY|O_RDONLY, 0);

	if (fd > -4096UL) {
		stream_append_s8(&error_stream, s8("os_begin_path_stream: failed to open: "));
		stream_append_s8(&error_stream, (s8){.len = dir_name->widx - 1, .s = dir_name->data});
		die(&error_stream);
	}

	LinuxDirectoryStream *lds = alloc(a, LinuxDirectoryStream, 1, arena_flags);
	lds->fd = fd;
	return (iptr)lds;
}

static void
os_end_path_stream(iptr path_stream)
{
	LinuxDirectoryStream *lds = (LinuxDirectoryStream *)path_stream;
	syscall1(SYS_close, (iptr)lds->fd);
}

static s8
os_get_valid_file(iptr path_stream, s8 match_prefix, Arena *a, u32 arena_flags)
{
	s8 result = {0};
	if (path_stream) {
		LinuxDirectoryStream *lds = (LinuxDirectoryStream *)path_stream;
		for (;;) {
			if (lds->buf_pos >= lds->buf_end) {
				u64 ret = syscall3(SYS_getdents64, lds->fd, (iptr)lds->buf,
				                   sizeof(lds->buf));
				if (ret > -4096UL) {
					stream_append_s8(&error_stream, s8("os_get_valid_file: SYS_getdents"));
					die(&error_stream);
				}
				if (ret == 0)
					break;
				lds->buf_end = ret;
				lds->buf_pos = 0;
			}
			u16  record_len = DIRENT_RECLEN(lds->buf + lds->buf_pos);
			u8   type       = DIRENT_TYPE(lds->buf + lds->buf_pos);
			char *name      = DIRENT_NAME(lds->buf + lds->buf_pos);
			lds->buf_pos += record_len;
			if (type == DT_REGULAR_FILE) {
				b32 valid = 1;
				for (size i = 0; i < match_prefix.len; i++) {
					if (match_prefix.s[i] != name[i]) {
						valid = 0;
						break;
					}
				}
				if (valid) {
					result = os_read_whole_file_at(name, lds->fd, a, arena_flags);
					break;
				}
			}
		}
	}
	return result;
}

void
linux_main(i32 argc, char *argv[], char *envp[])
{
	(void)envp;

	Arena memory = os_new_arena(1024 * MEGABYTE);

	error_stream.fd   = 2;
	error_stream.cap  = 4096;
	error_stream.data = alloc(&memory, u8, error_stream.cap, ARENA_NO_CLEAR);

	stdout_stream.fd   = 1;
	stdout_stream.cap  = 8 * MEGABYTE;
	stdout_stream.data = alloc(&memory, u8, error_stream.cap, ARENA_NO_CLEAR);

	i32 result = jdict(&memory, argc, argv);

	os_exit(result);
}
