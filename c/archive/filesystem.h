#ifndef __FILESYSTEM_H__
#define __FILESYSTEM_H__

#include <libbds/bds_vector.h>

#define CHECK_INSTALLED     0x1
#define CHECK_ANY_INSTALLED 0x2


const char *find_sbo_dir(const char *sbo_repo, const char *pkg_name);
const char *find_sbo_info(const char *sbo_repo, const char *pkg_name);
const char *find_sbo_readme(const char *sbo_repo, const char *pkg_name);
char *read_sbo_readme(const char *sbo_dir, const char *pkg_name);
const char *read_sbo_requires(const char *sbo_dir, const char *pkg_name);
int search_sbo_repo(const char *sbo_repo, const char *pkg_name, struct bds_vector **pkg_list);
bool is_pkg_installed(const char *pkg_name, const char *tag);
char *mmap_file(const char *path, size_t *size);
int munmap_file(char *data, size_t size);

#endif // __FILESYSTEM_H__
