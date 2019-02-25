#ifndef __PKG_UTIL_H__
#define __PKG_UTIL_H__

#include "pkg.h"

bool skip_installed(const char *pkg_name, struct pkg_options options);
int load_dep_file(pkg_list_t *pkg_list, const char *pkg_name, struct pkg_options options);
const char *create_default_dep(struct pkg *pkg);

#endif
