#ifndef FS_H

#include <stdlib.h>

int fs_readonly_map(const char* path, void** p, size_t* sz);
void fs_readonly_unmap(int fd);

int fs_write_open(const char* path);
void fs_write(int fd, void* p, size_t sz);
void fs_write_close(int fd);

#define FS_H
#endif
