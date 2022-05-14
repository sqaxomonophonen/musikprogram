#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __unix__
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "fs.h"

enum handle_type {
	FREE = 0,
	READONLY_MAP,
};

struct handle {
	enum handle_type type;
	union {
		struct {
			void* p;
			size_t sz;
		} readonly_map;
	};
};

#define N_HANDLES (1<<16)
struct handle handles[N_HANDLES];
int handle_cursor;

static int alloc()
{
	for (int i = 0; i < N_HANDLES; i++) {
		struct handle* h = &handles[handle_cursor];
		if (h->type == FREE) return handle_cursor;
		handle_cursor = (handle_cursor + 1) % N_HANDLES;
	}
	assert(!"no free file handle");
}

#ifdef __unix__

static const char* mappath(const char* path, char* buf, size_t bufsz)
{
	if (path[0] == '!') {
		snprintf(buf, bufsz, "%s/.local/share/%s", getenv("HOME"), path+1);
		return buf;
	} else {
		return path;
	}
}

int fs_readonly_map(const char* path, void** p, size_t* sz)
{
	char buf[65536];
	path = mappath(path, buf, sizeof buf);

	int fd = open(path, O_RDONLY);
	if (fd == -1) {
		return -1;
	}

	struct stat st;
	if (fstat(fd, &st) == -1) {
		close(fd);
		return -1;
	}

	if (sz) *sz = st.st_size;

	void* addr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	assert(close(fd) == 0);
	if (addr == MAP_FAILED) {
		return -1;
	}

	if (p) *p = addr;

	int handle = alloc();
	struct handle* h = &handles[handle];
	h->type = READONLY_MAP;
	h->readonly_map.p = addr;
	h->readonly_map.sz = st.st_size;

	return handle;
}

void fs_readonly_unmap(int handle)
{
	struct handle* h = &handles[handle];
	assert(h->type == READONLY_MAP);
	assert(munmap(h->readonly_map.p, h->readonly_map.sz) == 0);
	h->type = FREE;
}

int fs_write_open(const char* path)
{
	return 0;
}

void fs_write(int handle, void* p, size_t sz)
{
}

void fs_write_close(int handle)
{
}

#else

#error "missing implementation"

#endif
