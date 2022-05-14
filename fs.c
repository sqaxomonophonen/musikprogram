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

enum descriptor_type {
	FREE = 0,
	READONLY_MAP,
};

struct descriptor {
	enum descriptor_type type;
	union {
		struct {
			void* p;
			size_t sz;
		} readonly_map;
	};
};

#define N_DESCRIPTORS (1<<16)
struct descriptor descriptors[N_DESCRIPTORS];
int cursor;

static int alloc()
{
	for (int i = 0; i < N_DESCRIPTORS; i++) {
		cursor %= N_DESCRIPTORS;
		struct descriptor* h = &descriptors[cursor];
		if (h->type == FREE) return cursor;
		cursor++;
	}
	return -1;
}

#ifdef __unix__

static const char* mappath(const char* path, char* buf, size_t bufsz)
{
	if (path[0] == '!') {
		char* home = getenv("HOME");
		if (home == NULL) return NULL;
		#ifdef __APPLE__
		snprintf(buf, bufsz, "%s/Library/Preferences/%s", home, path+1);
		#else
		snprintf(buf, bufsz, "%s/.config/%s", home, path+1);
		#endif
		return buf;
	} else {
		return path;
	}
}

int fs_readonly_map(const char* path, void** p, size_t* sz)
{
	int handle = alloc();
	if (handle < 0) return -1;

	char buf[65536];
	path = mappath(path, buf, sizeof buf);
	if (path == NULL) return -1;

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

	struct descriptor* h = &descriptors[handle];
	h->type = READONLY_MAP;
	h->readonly_map.p = addr;
	h->readonly_map.sz = st.st_size;

	return handle;
}

void fs_readonly_unmap(int handle)
{
	struct descriptor* h = &descriptors[handle];
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
