#ifndef UPDATE_H__
#define UPDATE_H__

#include "pkg.h"
#include "pkg_graph.h"
#include "slack_pkg_dbi.h"
#include "string_list.h"

void print_update_help();

int process_update_options(int argc, char **argv, struct pkg_options *options);

int run_update_command(const struct slack_pkg_dbi *slack_pkg_dbi,
                       struct pkg_graph *          pkg_graph,
                       const string_list_t *       pkg_names,
                       struct pkg_options          pkg_options);

#endif // UPDATE_H__
