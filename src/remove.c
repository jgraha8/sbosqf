#include <assert.h>
#include <stdio.h>

#include <libbds/bds_stack.h>

#include "mesg.h"
#include "options.h"
#include "pkg.h"
#include "pkg_graph.h"
#include "pkg_io.h"
#include "slack_pkg_dbi.h"
#include "string_list.h"
#include "user_config.h"

void print_remove_help()
{
        printf("Usage: %s remove [option] pkg\n"
               "Options:\n"
               "  -d, --deep\n"
               "  -h, --help\n"
               "  -l, --list\n"
               "  -L, --list-slackpkg {1|2}\n"
               "  -o, --output\n"
               "  -R, --repo-db\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

int process_remove_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *        options_str    = "dhlL:o:R";
        static const struct option long_options[] = {                       /* These options set a flag. */
                                                     LONG_OPT("deep", 'd'), /* option */
                                                     LONG_OPT("help", 'h'),          LONG_OPT("list", 'l'),
                                                     LONG_OPT("list-slackpkg", 'L'), LONG_OPT("output", 'o'),
                                                     LONG_OPT("repo-db", 'R'),       {0, 0, 0, 0}};

        int rc = process_options(argc, argv, options_str, long_options, print_remove_help, options);

        /* Revdeps processing is required for package removal */
        options->revdeps = true;

        if (rc >= 0) {
                if (options->output_mode != PKG_OUTPUT_FILE && options->output_name) {
                        mesg_error(
                            "options --list/-l, --list-slackpkg/-L, and --output/-o are mutually exclusive\n");
                        return -1;
                }
        }

        return rc;
}

int run_remove_command(const struct slack_pkg_dbi *slack_pkg_dbi,
                       struct pkg_graph *          pkg_graph,
                       const string_list_t *       pkg_names,
                       struct pkg_options          pkg_options)
{

        int                  rc = 0;
        char                 sqf_file[256];
        struct ostream *     os           = NULL;
        pkg_nodes_t *        pkg_list     = NULL;
        struct bds_stack *   removal_list = NULL;
        struct pkg_node *    node         = NULL;
        struct pkg_iterator  iter;
        pkg_iterator_flags_t flags    = 0;
        int                  max_dist = 0;
        const size_t         num_pkgs = string_list_size(pkg_names);

        // Make sure revdeps flag is set
        assert(pkg_options.revdeps);

        if ((rc = pkg_load_all_deps(pkg_graph, pkg_options)) != 0)
                return rc;

        // Make sure all dependency files are loaded in case we have meta-packages
        for (size_t i = 0; i < num_pkgs; ++i) {
                if ((rc = pkg_load_dep(pkg_graph, string_list_get_const(pkg_names, i), pkg_options)) != 0)
                        return rc;
        }

        pkg_list     = pkg_nodes_alloc_reference();
        removal_list = bds_stack_alloc(1, sizeof(struct pkg_node *), NULL);

        flags    = PKG_ITER_DEPS | PKG_ITER_FORW;
        max_dist = (pkg_options.deep ? -1 : 0);

        for (size_t i = 0; i < num_pkgs; ++i) {
                const char *pkg_name = string_list_get_const(pkg_names, i);

                for (node = pkg_iterator_begin(&iter, pkg_graph, pkg_name, flags, max_dist); node != NULL;
                     node = pkg_iterator_next(&iter)) {

                        if (node->pkg.dep.is_meta)
                                continue;

                        if (!slack_pkg_dbi->is_installed(node->pkg.name, user_config.sbo_tag))
                                continue;

                        node->pkg.for_removal = true;
                        if (pkg_nodes_lsearch_const(pkg_list, node->pkg.name) == NULL)
                                pkg_nodes_append(pkg_list, node);
                }
                pkg_iterator_destroy(&iter);
        }

        /*
          Check if packages marked for removal have any parent packages installed

          Forward traversal through the package graph
         */
        for (size_t i = 0; i < pkg_nodes_size(pkg_list); ++i) {
                node     = pkg_nodes_get(pkg_list, i);
                flags    = PKG_ITER_REVDEPS;
                max_dist = 1;

                for (struct pkg_node *parent_node =
                         pkg_iterator_begin(&iter, pkg_graph, node->pkg.name, flags, max_dist);
                     parent_node != NULL; parent_node = pkg_iterator_next(&iter)) {

                        if (strcmp(parent_node->pkg.name, node->pkg.name) == 0)
                                continue;

                        if (parent_node->pkg.dep.is_meta)
                                continue;

                        bool parent_installed =
                            slack_pkg_dbi->is_installed(parent_node->pkg.name, user_config.sbo_tag);
                        if (!parent_node->pkg.for_removal && parent_installed) {
                                mesg_error_label("%12s", " %-24s <-- %s\n", "[required]", node->pkg.name,
                                                 parent_node->pkg.name);
                                node->pkg.for_removal = false; /* Disable package remove */
                                break;
                        }
                }
                pkg_iterator_destroy(&iter);

                if (node->pkg.for_removal)
                        bds_stack_push(removal_list, &node);
        }

        if (bds_stack_size(removal_list) == 0) /* Empty removal list */
                goto finish;

        if (pkg_options.output_mode == PKG_OUTPUT_FILE && num_pkgs > 1)
                assert(pkg_options.output_name);

        if (pkg_options.output_name) {
                bds_string_copyf(sqf_file, sizeof(sqf_file), "%s", pkg_options.output_name);
        } else {
                bds_string_copyf(sqf_file, sizeof(sqf_file), "%s-remove.sqf", string_list_get_const(pkg_names, 0));
        }

        bool        buffer_stream = (pkg_options.output_mode != PKG_OUTPUT_FILE);
        const char *output_path   = (pkg_options.output_mode == PKG_OUTPUT_FILE ? &sqf_file[0] : "/dev/stdout");
        os                        = ostream_open(output_path, "w", buffer_stream);

        while (bds_stack_pop(removal_list, &node)) {
                ostream_printf(os, "%s", pkg_output_name(pkg_options.output_mode, node->pkg.name));
        }

        if (pkg_options.output_mode == PKG_OUTPUT_FILE) {
                mesg_ok("created %s\n", sqf_file);
        } else {
                ostream_printf(os, "\n");
        }
        ostream_close(os);

finish:
        if (pkg_list)
                pkg_nodes_free(&pkg_list);
        if (removal_list)
                bds_stack_free(&removal_list);

        return rc;
}
