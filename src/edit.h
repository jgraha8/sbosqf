#ifndef EDIT_H__
#define EDIT_H__

#include "pkg.h"
#include "pkg_graph.h"

void print_edit_help();

int process_edit_options(int argc, char **argv, struct pkg_options *options);

int run_edit_command(struct pkg_graph *pkg_graph, const char *pkg_name);

#endif // EDIT_H__
