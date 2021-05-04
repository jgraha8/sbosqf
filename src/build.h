#ifndef BUILD_H__
#define BUILD_H__

#include "pkg.h"
#include "pkg_graph.h"
#include "slack_pkg_dbi.h"
#include "string_list.h"

void print_build_help();

int process_build_options(int argc, char **argv, struct pkg_options *options);

int run_build_command(const struct slack_pkg_dbi *slack_pkg_dbi,
		      struct pkg_graph *          pkg_graph,
		      string_list_t *             pkg_names,
		      struct pkg_options          pkg_options);

#endif // BUILD_H__
