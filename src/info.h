#ifndef INFO_H__
#define INFO_H__

#include "pkg.h"
#include "pkg_graph.h"

void print_info_help();

int process_info_options(int argc, char **argv, struct pkg_options *options);

int run_info_command(struct pkg_graph *pkg_graph, const char *pkg_name);

#endif // INFO_H__
