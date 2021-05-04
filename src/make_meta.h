#ifndef MAKE_META_H__
#define MAKE_META_H__

#include "pkg.h"
#include "pkg_graph.h"

void print_make_meta_help();

int process_make_meta_options(int argc, char **argv, struct pkg_options *options);

int run_make_meta_command(const pkg_nodes_t *sbo_pkgs, const char *meta_pkg_name, string_list_t *pkg_names);

#endif // MAKE_META_H__
