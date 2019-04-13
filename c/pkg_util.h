#ifndef __PKG_UTIL_H__
#define __PKG_UTIL_H__

#include "pkg.h"

void free_string_ptr(char **str);
bool skip_installed(const char *pkg_name, struct pkg_options options);
int load_dep_file(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options options);
const char *create_default_dep(struct pkg *pkg);
bool is_meta_pkg(const char *pkg_name);

#endif
