#ifndef __FILE_MMAP_H__
#define __FILE_MMAP_H__

struct file_mmap {
        char *data;
        size_t data_len;
};

struct file_mmap *file_mmap(const char *path);
int file_munmap(struct file_mmap **fm);

#endif // __FILE_MMAP_H__
