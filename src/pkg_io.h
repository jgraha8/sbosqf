#ifndef PKG_IO_H__
#define PKG_IO_H__

#include "string_list.h"
#include "pkg.h"
#include "pkg_graph.h"
#include "slack_pkg_dbi.h"
#include "ostream.h"

bool pkg_db_exists();
int pkg_write_db(pkg_nodes_t *pkgs);

int pkg_load_db(pkg_nodes_t *pkgs);
int pkg_load_sbo(pkg_nodes_t *pkgs);

int pkg_load_installed_deps(const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph, struct pkg_options options);

int pkg_load_dep(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options options);
int pkg_load_all_deps(struct pkg_graph *pkg_graph, struct pkg_options options);

int pkg_write_sqf(struct ostream *os, const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph,
		 const string_list_t *pkg_names, struct pkg_options options, bool *db_dirty);

int pkg_load_dep(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options options);

#endif // IO_H__
