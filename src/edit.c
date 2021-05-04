#include <assert.h>
#include <stdio.h>

#include "mesg.h"
#include "config.h"
#include "options.h"
#include "pkg.h"
#include "pkg_graph.h"
#include "pkg_io.h"
#include "pkg_ops.h"
#include "user_config.h"

void print_edit_help()
{
        printf("Usage: %s edit [option] pkg\n"
               "Options:\n"
               "  -h, --help\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

int process_edit_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *        options_str    = "h";
        static const struct option long_options[] = {LONG_OPT("help", 'h'), {0, 0, 0, 0}};

        return process_options(argc, argv, options_str, long_options, print_edit_help, options);
}

int run_edit_command(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        int              rc       = 0;
        struct pkg_node *pkg_node = NULL;

        pkg_node = (struct pkg_node *)pkg_graph_search(pkg_graph, pkg_name);
        if (pkg_node == NULL) {
                mesg_error("package %s does not exist\n", pkg_name);
                return 1;
        }

        rc = pkg_edit_dep(pkg_node->pkg.name);
        if (rc != 0)
                return rc;

        /*
          Mark not reviewed
         */
        pkg_node->pkg.is_reviewed = false;

        return pkg_write_db(pkg_graph_sbo_pkgs(pkg_graph));
}
