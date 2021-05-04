#ifndef SEARCH_H__
#define SEARCH_H__

#include "pkg.h"
#include "pkg_graph.h"

void print_search_help();

int process_search_options(int argc, char **argv, struct pkg_options *options);

int run_search_command(struct pkg_graph *pkg_graph, const char *pkg_name);

#endif // SEARCH_H__
