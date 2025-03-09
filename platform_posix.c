#define _DEFAULT_SOURCE 1
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdint.h>
#include <stddef.h>
typedef uint8_t   u8;
typedef int64_t   i64;
typedef uint64_t  u64;
typedef int32_t   i32;
typedef uint32_t  u32;
typedef uint32_t  b32;
typedef uint16_t  u16;
typedef ptrdiff_t size;
typedef size_t    usize;
typedef ptrdiff_t iptr;

#define os_path_sep s8("/")

#include "jdict.c"

static void
os_exit(i32 code)
{
	_exit(code);
	unreachable();
}

static Arena
os_new_arena(size cap)
{
	Arena a;

	size pagesize = sysconf(_SC_PAGESIZE);
	if (cap % pagesize != 0)
		cap += pagesize - cap % pagesize;

	a.beg = mmap(0, cap, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
	if (a.beg == MAP_FAILED)
		return (Arena){0};
	a.end = a.beg + cap;
#ifdef _DEBUG_ARENA
	a.min_capacity_remaining = cap;
#endif
	return a;
}

static b32
os_read_stdin(u8 *buf, size count)
{
	size rlen = read(STDIN_FILENO, buf, count);
	return rlen == count;
}

static s8
os_read_whole_file_at(char *file, iptr dir_fd, Arena *a, u32 arena_flags)
{

	i32 fd = openat(dir_fd, file, O_RDONLY);
	if (fd < 0) {
		stream_append_s8(&error_stream, s8("failed to open: "));
		stream_append_s8(&error_stream, cstr_to_s8(file));
		die(&error_stream);
	}

	struct stat st;
	if (fstat(fd, &st) < 0) {
		stream_append_s8(&error_stream, s8("failed to stat: "));
		stream_append_s8(&error_stream, cstr_to_s8(file));
		die(&error_stream);
	}

	s8 result = {.len = st.st_size, .s = alloc(a, u8, st.st_size, arena_flags|ARENA_NO_CLEAR)};
	size rlen = read(fd, result.s, result.len);
	close(fd);

	if (rlen != result.len) {
		stream_append_s8(&error_stream, s8("failed to read whole file: "));
		stream_append_s8(&error_stream, cstr_to_s8(file));
		die(&error_stream);
	}

	return result;
}

static b32
os_write(iptr file, s8 raw)
{
	while (raw.len) {
		size r = write(file, raw.s, raw.len);
		if (r < 0) return 0;
		raw = s8_cut_head(raw, r);
	}
	return 1;
}

static iptr
os_begin_path_stream(Stream *dir_name, Arena *a, u32 arena_flags)
{
	(void)a; (void)arena_flags;

	stream_append_byte(dir_name, 0);
	DIR *dir = opendir((char *)dir_name->data);
	if (!dir) {
		stream_append_s8(&error_stream, s8("opendir: failed to open: "));
		stream_append_s8(&error_stream, (s8){.len = dir_name->widx - 1, .s = dir_name->data});
		die(&error_stream);
	}
	return (iptr)dir;
}

static s8
os_get_valid_file(iptr path_stream, s8 match_prefix, Arena *a, u32 arena_flags)
{
	s8 result = {0};
	if (path_stream) {
		DIR *dir    = (DIR *)path_stream;
		iptr dir_fd = dirfd(dir);
		struct dirent *dent;
		while ((dent = readdir(dir)) != NULL) {
			if (dent->d_type == DT_REG) {
				b32 valid = 1;
				for (size i = 0; i < match_prefix.len; i++) {
					if (match_prefix.s[i] != dent->d_name[i]) {
						valid = 0;
						break;
					}
				}
				if (valid) {
					result = os_read_whole_file_at(dent->d_name, dir_fd, a, arena_flags);
					break;
				}
			}
		}
	}
	return result;
}

static void
os_end_path_stream(iptr path_stream)
{
	closedir((DIR *)path_stream);
}

i32
main(i32 argc, char *argv[])
{
	Arena memory = os_new_arena(1024 * MEGABYTE);

	error_stream.fd   = STDERR_FILENO;
	error_stream.cap  = 4096;
	error_stream.data = alloc(&memory, u8, error_stream.cap, ARENA_NO_CLEAR);

	stdout_stream.fd   = STDOUT_FILENO;
	stdout_stream.cap  = 8 * MEGABYTE;
	stdout_stream.data = alloc(&memory, u8, stdout_stream.cap, ARENA_NO_CLEAR);

	return jdict(&memory, argc, argv);
}
