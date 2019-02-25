#ifndef __SBO_H__
#define __SBO_H__

#include <libbds/bds_vector.h>

const char *sbo_find_dir(const char *sbo_repo, const char *pkg_name);
const char *sbo_find_info(const char *sbo_repo, const char *pkg_name);
const char *sbo_find_readme(const char *sbo_repo, const char *pkg_name);
char *sbo_load_readme(const char *sbo_dir, const char *pkg_name);
const char *sbo_read_requires(const char *sbo_dir, const char *pkg_name);
int sbo_search_repo(const char *sbo_repo, const char *pkg_name, struct bds_vector **pkg_list);
bool is_pkg_installed(const char *pkg_name, const char *tag);
char *mmap_file(const char *path, size_t *size);
int munmap_file(char *data, size_t size);

#endif // __SBO_H__
