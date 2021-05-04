#ifndef REVIEW_H__
#define REVIEW_H__

#include "pkg.h"
#include "pkg_graph.h"

void print_review_help();

int process_review_options(int argc, char **argv, struct pkg_options *options);

int run_review_command(struct pkg_graph *pkg_graph, const char *pkg_name);

#endif // REVIEW_H__
