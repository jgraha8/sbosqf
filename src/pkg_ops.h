#ifndef __PKG_OPS_H__
#define __PKG_OPS_H__

#include <stdbool.h>

#include "pkg_graph.h"
#include "slack_pkg_dbi.h"

int pkg_create_default_deps(pkg_nodes_t *pkgs);
int pkg_review(const struct pkg *pkg);
int pkg_review_prompt(const struct pkg *pkg, bool return_on_modify_mask, int *dep_status);
int pkg_show_info(const struct pkg *pkg);
int pkg_compare_sets(pkg_nodes_t *new_pkgs, pkg_nodes_t *old_pkgs);
int pkg_edit_dep(const char *pkg_name);

#endif
