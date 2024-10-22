/* See LICENSE for license details. */
#define os_path_sep s8("/")

#define NULL ((void *)0)

#include "jdict.c"

#define PROT_READ     0x01
#define PROT_WRITE    0x02
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20

#define O_RDONLY      0x00
#define O_DIRECTORY   0x10000

#define DT_REGULAR_FILE 8

static i64 syscall1(i64, i64);
static i64 syscall2(i64, i64, i64);
static i64 syscall3(i64, i64, i64, i64);
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
	__builtin_unreachable();
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
os_read_whole_file(char *file, Arena *a, u32 arena_flags)
{
	__attribute((aligned(16))) u8 stat_buf[STAT_BUF_SIZE];
	u64 status = syscall2(SYS_stat, (iptr)file, (iptr)stat_buf);
	if (status > -4096UL) {
		stream_append_s8(&error_stream, s8("failed to stat: "));
		stream_append_s8(&error_stream, cstr_to_s8(file));
		die(&error_stream);
	}

	u64 fd = syscall3(SYS_open, (iptr)file, O_RDONLY, 0);
	if (fd > -4096UL) {
		stream_append_s8(&error_stream, s8("failed to open: "));
		stream_append_s8(&error_stream, cstr_to_s8(file));
		die(&error_stream);
	}

	i64 *file_size = (i64 *)(stat_buf + STAT_SIZE_OFF);
	s8 result = {.len = *file_size, .s = alloc(a, u8, *file_size, arena_flags|ARENA_NO_CLEAR)};
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
os_new_arena(size cap)
{
	Arena a = {0};

	size pagesize = PAGESIZE;
	if (cap % pagesize != 0)
		cap += pagesize - cap % pagesize;

	u64 p = syscall6(SYS_mmap, 0, cap, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
	if (p > -4096UL) {
		return (Arena){0};
	} else {
		a.beg = (u8 *)p;
	}
	a.end = a.beg + cap;
#ifdef _DEBUG_ARENA
	a.min_capacity_remaining = cap;
#endif
	return a;
}

static PathStream
os_begin_path_stream(Stream *dir_name, Arena *a, u32 arena_flags)
{
	stream_append_byte(dir_name, 0);
	u64 fd = syscall3(SYS_open, (iptr)dir_name->data, O_DIRECTORY|O_RDONLY, 0);
	dir_name->widx--;

	if (fd > -4096UL) {
		stream_append_s8(&error_stream, s8("os_begin_path_stream: failed to open: "));
		stream_append_s8(&error_stream, (s8){.len = dir_name->widx, .s = dir_name->data});
		die(&error_stream);
	}

	stream_append_byte(dir_name, '/');

	LinuxDirectoryStream *lds = alloc(a, LinuxDirectoryStream, 1, arena_flags);
	lds->fd = fd;
	return (PathStream){.dir_name = dir_name, .dirfd = lds};
}

static void
os_end_path_stream(PathStream *ps)
{
	LinuxDirectoryStream *lds = ps->dirfd;
	syscall1(SYS_close, (iptr)lds->fd);
	ps->dirfd = 0;
}

static s8
os_get_valid_file(PathStream *ps, s8 match_prefix, Arena *a, u32 arena_flags)
{
	s8 result = {0};
	LinuxDirectoryStream *lds = ps->dirfd;
	if (lds) {
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
			u16  record_len = *(u16 *)(lds->buf + lds->buf_pos + DIRENT_RECLEN_OFF);
			u8   type       = lds->buf[lds->buf_pos + DIRENT_TYPE_OFF];
			/* NOTE: technically this contains extra NULs but it doesn't matter
			 * for this purpose. We need NUL terminated to call SYS_read */
			s8   name       = {.len = record_len - 2 - DIRENT_NAME_OFF,
			                   .s = lds->buf + lds->buf_pos + DIRENT_NAME_OFF};
			lds->buf_pos += record_len;
			if (type == DT_REGULAR_FILE) {
				b32 valid = 1;
				for (size i = 0; i < match_prefix.len; i++) {
					if (match_prefix.s[i] != name.s[i]) {
						valid = 0;
						break;
					}
				}
				if (valid) {
					Stream dir_name = *ps->dir_name;
					stream_append_s8(&dir_name, name);
					result = os_read_whole_file((char *)dir_name.data, a,
					                            arena_flags);
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
