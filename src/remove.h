#ifndef REMOVE_H__
#define REMOVE_H__

void print_remove_help();

int process_remove_options(int argc, char **argv, struct pkg_options *options);

int run_remove_command(const struct slack_pkg_dbi *slack_pkg_dbi,
		       struct pkg_graph *          pkg_graph,
		       const string_list_t *       pkg_names,
		       struct pkg_options          pkg_options);

#endif // REMOVE_H__
