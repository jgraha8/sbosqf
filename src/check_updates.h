#ifndef CHECK_UPDATES_H__
#define CHECK_UPDATES_H__

void print_check_updates_help();

int process_check_updates_options(int argc, char **argv, struct pkg_options *options);

int run_check_updates_command(const struct slack_pkg_dbi *slack_pkg_dbi,
                              struct pkg_graph *          pkg_graph,
                              const char *                pkg_name);

#endif // CHECK_UPDATES_H__
