#ifndef __PKG_UTIL_H__
#define __PKG_UTIL_H__

#include "pkg.h"

void free_string_ptr(char **str);
bool skip_installed(const char *pkg_name, struct pkg_options options);
int load_dep_file(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options options);
bool file_exists(const char *pathname);
bool dep_file_exists(const char *pkg_name);
const char *create_default_dep(struct pkg *pkg);
const char *create_default_dep_verbose(struct pkg *pkg);
bool is_meta_pkg(const char *pkg_name);

#endif
