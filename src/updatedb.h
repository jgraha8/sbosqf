#ifndef UPDATEDB_H__
#define UPDATEDB_H__

void print_updatedb_help();

int process_updatedb_options(int argc, char **argv, struct pkg_options *options);

int run_updatedb(struct pkg_graph *pkg_graph);

#endif // UPDATEDB_H__
