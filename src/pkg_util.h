#ifndef __PKG_UTIL_H__
#define __PKG_UTIL_H__

#include "ostream.h"
#include "pkg_graph.h"
#include "slack_pkg_dbi.h"
#include "string_list.h"

void free_string_ptr(char **str);
bool check_installed(const struct slack_pkg_dbi *slack_pkg_dbi, const char *pkg_name, struct pkg_options options);
bool file_exists(const char *pathname);

const char *create_default_dep(const struct pkg *pkg);
const char *create_default_dep_verbose(const struct pkg *pkg);

int pkg_edit_dep_file(const char *pkg_name);
bool pkg_is_meta(const char *pkg_name);

bool pkg_dep_file_exists(const char *pkg_name);
bool pkg_skip_dep_line(char *line);
int pkg_compare_versions(const char *ver_a, const char *ver_b);
int pkg_check_reviewed(struct pkg *pkg, enum pkg_review_type review_type, bool *db_dirty);

#endif
