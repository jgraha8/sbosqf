#include <stdio.h>

#include "options.h"
#include "pkg.h"
#include "pkg_graph.h"
#include "pkg_ops.h"
#include "mesg.h"

void print_info_help()
{
        printf("Usage: %s info [option] pkg\n"
               "Options:\n"
               "  -h, --help\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

int process_info_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *        options_str    = "h";
        static const struct option long_options[] = {LONG_OPT("help", 'h'), {0, 0, 0, 0}};

        return process_options(argc, argv, options_str, long_options, print_info_help, options);
}

int run_info_command(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        const struct pkg_node *pkg_node = NULL;

        pkg_node = (const struct pkg_node *)pkg_graph_search(pkg_graph, pkg_name);
        if (pkg_node == NULL) {
                mesg_error("package %s does not exist\n", pkg_name);
                return 1;
        }
        return pkg_show_info(&pkg_node->pkg);
}
