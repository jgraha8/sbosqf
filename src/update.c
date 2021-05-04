#include <assert.h>
#include <dirent.h>
#include <stdio.h>

#include "mesg.h"
#include "options.h"
#include "output_path.h"
#include "pkg.h"
#include "pkg_graph.h"
#include "pkg_io.h"
#include "pkg_util.h"
#include "user_config.h"

void print_update_help()
{
        printf("Usage: %s update [option] pkg\n"
               "Options:\n"
               "  -A, --auto-review-verbose\n"
               "  -a, --auto-review\n"
               "  -i, --ignore-review\n"
               "  -h, --help\n"
               "  -l, --list\n"
               "  -L, --list-slackpkg {1|2}\n"
               "  -o, --output\n"
               "  -P, --installed-revdeps\n"
               "  -R, --repo-db\n"
               "  -r, --rebuild-deps\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

int process_update_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *        options_str    = "AaihlL:o:PRrz";
        static const struct option long_options[] = {/* These options set a flag. */
                                                     LONG_OPT("auto-review-verbose", 'A'), /* option */
                                                     LONG_OPT("auto-review", 'a'),         /* option */
                                                     LONG_OPT("ignore-review", 'i'),       /* option */
                                                     LONG_OPT("help", 'h'),
                                                     LONG_OPT("list", 'l'),
                                                     LONG_OPT("list-slackpkg", 'L'),
                                                     LONG_OPT("output", 'o'),
                                                     LONG_OPT("installed-revdeps", 'P'), /* option */
                                                     LONG_OPT("repo-db", 'R'),
                                                     LONG_OPT("rebuild-deps", 'r'),
                                                     {0, 0, 0, 0}};

        int rc = process_options(argc, argv, options_str, long_options, print_update_help, options);

        if (rc >= 0) {
                if (options->output_mode != PKG_OUTPUT_FILE && options->output_name) {
                        mesg_error(
                            "options --list/-l, --list-slackpkg/-L, and --output/-o are mutually exclusive\n");
                        return -1;
                }
        }

        return rc;
}

/**
   @brief Selects packages from a specified list that have been updated in the SBo repository from what is
   currently in the package database.

   @param pkg_graph Address of the package graph object
   @param pkg_names Address of the string list object containing the list of specified package names
   @return updated_pkgs Address of the package nodes object containing the set of updated packages
   @retval Returns 0 on success and -1 otherwise.

   @note This procedure requires that all packages (including meta packages) provided by @c pkg_names are already
   loaded into the package graph.
 */
static int select_updated_pkgs(const struct slack_pkg_dbi *slack_pkg_dbi,
                               struct pkg_graph *          pkg_graph,
                               const string_list_t *       pkg_names,
                               pkg_nodes_t *               updated_pkgs)
{
        struct pkg_node *       node      = NULL;
        const struct slack_pkg *slack_pkg = NULL;
        const char *            pkg_name  = NULL;
        const int               flags     = PKG_ITER_DEPS;
        const int               max_dist  = 0;
        struct pkg_iterator     iter;

        for (size_t i = 0; i < string_list_size(pkg_names); ++i) {
                pkg_name = string_list_get_const(pkg_names, i);

                for (node = pkg_iterator_begin(&iter, pkg_graph, pkg_name, flags, max_dist); node != NULL;
                     node = pkg_iterator_next(&iter)) {

                        if (node->pkg.dep.is_meta)
                                continue;

                        slack_pkg = slack_pkg_dbi->search_const(node->pkg.name, user_config.sbo_tag);
                        if (slack_pkg) {
                                if (pkg_compare_versions(node->pkg.version, slack_pkg->version) > 0) {
                                        pkg_nodes_append_unique(updated_pkgs, node);
                                }
                        }
                }
                pkg_iterator_destroy(&iter);
        }

        return 0;
}

static int process_update_deps(const struct slack_pkg_dbi *slack_pkg_dbi,
                               struct pkg_graph *          pkg_graph,
                               const bool                  rebuild_deps,
                               pkg_nodes_t *               pkg_list,
                               pkg_nodes_t *               update_list,
                               pkg_nodes_t *               build_list)
{
        int rc = 0;

        assert(pkg_nodes_size(update_list) == 0);

        while (pkg_nodes_size(pkg_list) > 0) {

                struct pkg_node *cur_node = pkg_nodes_get(pkg_list, 0);

                /*
                  Process dependencies
                */
                int                 flags    = PKG_ITER_DEPS | PKG_ITER_PRESERVE_COLOR;
                int                 max_dist = -1;
                struct pkg_iterator iter;

                for (struct pkg_node *node =
                         pkg_iterator_begin(&iter, pkg_graph, cur_node->pkg.name, flags, max_dist);
                     node != NULL; node = pkg_iterator_next(&iter)) {

                        if (node->pkg.dep.is_meta)
                                continue;

                        /*
                          We need to check if an update type (for a parent package) has already been marked by a
                          call to update_process_revdeps().
                        */
                        if (PKG_REVDEP_UPDATE == node->pkg.update.type ||
                            PKG_REVDEP_REBUILD == node->pkg.update.type) {
                                assert(0 == node->dist);

                                pkg_nodes_append_unique(build_list, node);
                                continue;
                        }

                        const struct slack_pkg *slack_pkg =
                            slack_pkg_dbi->search_const(node->pkg.name, user_config.sbo_tag);

                        if (0 == node->dist) {
                                assert(slack_pkg);

                                if (PKG_UPDATE_NONE == node->pkg.update.type) {
                                        node->pkg.update = pkg_update_assign(PKG_UPDATE, NULL, slack_pkg->version);
                                        pkg_nodes_append_unique(update_list, node);
                                }
                                pkg_nodes_append_unique(build_list, node);
                                continue;
                        }

                        if (NULL == slack_pkg) {
                                assert(0 != node->dist);

                                /*
                                  Added dependency package

                                  If the package is not currently installed, we assume that it is a new
                                  dependency and add it to the output build list.
                                */
                                if (PKG_UPDATE_NONE == node->pkg.update.type) {
                                        node->pkg.update = pkg_update_assign(PKG_DEP_ADDED, cur_node, NULL);
                                }
                                pkg_nodes_append_unique(build_list, node);
                                continue;
                        }

                        int ver_diff = pkg_compare_versions(node->pkg.version, slack_pkg->version);
                        if (ver_diff > 0) {
                                /*
                                  Updated package (version change)

                                  If there is a version change we place the package in the output build
                                  list and added it to the next package list set for further processing.
                                */
                                if (PKG_UPDATE_NONE == node->pkg.update.type) {
                                        node->pkg.update =
                                            pkg_update_assign(PKG_DEP_UPDATE, cur_node, slack_pkg->version);
                                        pkg_nodes_append_unique(update_list, node);
                                }
                                pkg_nodes_append_unique(build_list, node);

                        } else if (ver_diff == 0) {
                                /*
                                  Dependency rebuild (no version change)

                                  The package is addded to the output build list. No further processing.
                                */
                                if (rebuild_deps) {
                                        if (PKG_UPDATE_NONE == node->pkg.update.type) {
                                                node->pkg.update =
                                                    pkg_update_assign(PKG_DEP_REBUILD, cur_node, NULL);
                                        }
                                        pkg_nodes_append_unique(build_list, node);
                                }
                        } else {
                                /*
                                  Dependency downgrade
                                */
                                if (PKG_UPDATE_NONE == node->pkg.update.type) {
                                        node->pkg.update =
                                            pkg_update_assign(PKG_DEP_DOWNGRADE, cur_node, slack_pkg->version);
                                }
                                pkg_nodes_append_unique(build_list, node);
                        }
                }
                pkg_iterator_destroy(&iter);

                pkg_nodes_remove(pkg_list, cur_node->pkg.name);
        }

        return rc;
}

static int process_update_revdeps(const struct slack_pkg_dbi *slack_pkg_dbi,
                                  struct pkg_graph *          pkg_graph,
                                  pkg_nodes_t *               pkg_list,
                                  pkg_nodes_t *               update_list,
                                  pkg_nodes_t *               build_list)
{
        int rc = 0;

        assert(pkg_nodes_size(pkg_list) == 0);

        while (pkg_nodes_size(update_list) > 0) {

                struct pkg_node *cur_node = pkg_nodes_get(update_list, 0);

                /*
                  Process parents for updated nodes
                */
                int                 flags    = PKG_ITER_REVDEPS | PKG_ITER_FORW | PKG_ITER_PRESERVE_COLOR;
                int                 max_dist = 1;
                struct pkg_iterator iter;

                for (struct pkg_node *node =
                         pkg_iterator_begin(&iter, pkg_graph, cur_node->pkg.name, flags, max_dist);
                     node != NULL; node = pkg_iterator_next(&iter)) {

                        if (node->pkg.dep.is_meta)
                                continue;

                        if (0 == node->dist)
                                continue;

                        const struct slack_pkg *slack_pkg =
                            slack_pkg_dbi->search_const(node->pkg.name, user_config.sbo_tag);

                        if (NULL == slack_pkg) {
                                continue;
                        }

                        int ver_diff = pkg_compare_versions(node->pkg.version, slack_pkg->version);
                        if (ver_diff > 0) {
                                /*
                                  Update parent package
                                */
                                if (PKG_UPDATE_NONE == node->pkg.update.type) {
                                        node->pkg.update =
                                            pkg_update_assign(PKG_REVDEP_UPDATE, cur_node, slack_pkg->version);
                                        pkg_nodes_append_unique(update_list, node);
                                }
                                pkg_nodes_append_unique(pkg_list, node);

                        } else if (ver_diff == 0) {
                                /*
                                  Rebuild parent package
                                */
                                if (PKG_UPDATE_NONE == node->pkg.update.type) {
                                        node->pkg.update = pkg_update_assign(PKG_REVDEP_REBUILD, cur_node, NULL);
                                }
                                pkg_nodes_append_unique(pkg_list, node);

                        } else {
                                /*
                                  Downgraded parent package
                                 */
                                if (PKG_UPDATE_NONE == node->pkg.update.type) {
                                        node->pkg.update =
                                            pkg_update_assign(PKG_REVDEP_DOWNGRADE, cur_node, slack_pkg->version);
                                        // Add it to the build list without further processing. These will get
                                        // removed upon before final processing, but are here for noting that a
                                        // downgraded parent package exists.
                                        pkg_nodes_append_unique(build_list, node);
                                }
                        }
                }
                pkg_iterator_destroy(&iter);

                pkg_nodes_remove(update_list, cur_node->pkg.name);
        }

        return rc;
}

static int process_update(const struct slack_pkg_dbi *slack_pkg_dbi,
                          struct pkg_graph *          pkg_graph,
                          struct pkg_options          pkg_options,
                          pkg_nodes_t *               pkg_list,
                          pkg_nodes_t *               update_list,
                          pkg_nodes_t *               build_list)
{
        int          rc               = 0;
        bool         db_dirty         = false;
        pkg_nodes_t *input_pkg_list   = pkg_nodes_alloc_reference();
        pkg_nodes_t *review_skip_list = pkg_nodes_alloc_reference();

        const enum pkg_review_type review_type  = pkg_options.review_type;
        const bool                 rebuild_deps = pkg_options.rebuild_deps;

        pkg_nodes_append_all(input_pkg_list, pkg_list);

        while (1) {

                pkg_graph_clear_markers(pkg_graph, false);

                for (size_t i = 0; i < pkg_nodes_size(build_list); ++i) {
                        pkg_update_reset(&pkg_nodes_get(build_list, i)->pkg.update);
                }
                pkg_nodes_clear(build_list);

                if (0 == pkg_nodes_size(pkg_list)) {
                        pkg_nodes_append_all(pkg_list, input_pkg_list);
                }

                while (pkg_nodes_size(pkg_list) + pkg_nodes_size(update_list) > 0) {
                        rc = process_update_deps(slack_pkg_dbi, pkg_graph, rebuild_deps, pkg_list, update_list,
                                                 build_list);
                        if (rc != 0) {
                                goto finish;
                        }

                        rc = process_update_revdeps(slack_pkg_dbi, pkg_graph, pkg_list, update_list, build_list);
                        if (rc != 0) {
                                goto finish;
                        }
                }

                for (size_t i = 0; i < pkg_nodes_size(build_list); ++i) {
                        struct pkg_node *node = pkg_nodes_get(build_list, i);

                        if (pkg_nodes_bsearch_const(review_skip_list, node->pkg.name)) {
                                continue;
                        }

                        rc = pkg_check_reviewed(&pkg_nodes_get(build_list, i)->pkg, review_type, &db_dirty);
                        if (0 > rc) {
                                goto finish;
                        }

                        if (db_dirty) {
                                rc = pkg_write_db(pkg_graph_sbo_pkgs(pkg_graph));
                                if (rc != 0) {
                                        goto finish;
                                }
                        }

                        pkg_nodes_insert_sort(review_skip_list, node);
                        if (0 < rc) {
                                break;
                        }
                }

                if (0 == rc) {
                        break;
                }
        }

finish:
        if (input_pkg_list) {
                pkg_nodes_free(&input_pkg_list);
        }
        if (review_skip_list) {
                pkg_nodes_free(&review_skip_list);
        }

        return rc;
}

static int write_sqf(const struct slack_pkg_dbi *slack_pkg_dbi,
                     struct pkg_graph *          pkg_graph,
                     string_list_t *             pkg_names,
                     const char *                output_path,
                     struct pkg_options          pkg_options,
                     bool *                      db_dirty)
{
        int rc = 0;

        struct ostream *os = ostream_open(output_path, "w", (0 == strcmp(output_path, "/dev/stdout")));

        if (os == NULL) {
                mesg_error("unable to create %s\n", output_path);
                return 1;
        }
        rc = pkg_write_sqf(os, slack_pkg_dbi, pkg_graph, pkg_names, pkg_options, db_dirty);

        if (os)
                ostream_close(os);

        return rc;
}

static int write_nodes_sqf(const struct slack_pkg_dbi *slack_pkg_dbi,
                           struct pkg_graph *          pkg_graph,
                           pkg_nodes_t *               pkg_nodes,
                           const char *                output_path,
                           struct pkg_options          pkg_options,
                           bool *                      db_dirty)
{
        int rc = 0;

        string_list_t *pkg_names = string_list_alloc_reference();

        for (size_t i = 0; i < pkg_nodes_size(pkg_nodes); ++i) {
                string_list_append(pkg_names, pkg_nodes_get_const(pkg_nodes, i)->pkg.name);
        }

        rc = write_sqf(slack_pkg_dbi, pkg_graph, pkg_names, output_path, pkg_options, db_dirty);

        string_list_free(&pkg_names);

        return rc;
}

int run_update_command(const struct slack_pkg_dbi *slack_pkg_dbi,
                       struct pkg_graph *          pkg_graph,
                       const string_list_t *       pkg_names,
                       struct pkg_options          pkg_options)
{
        int rc = 0;

        pkg_nodes_t *pkg_list    = NULL;
        pkg_nodes_t *update_list = NULL;
        pkg_nodes_t *build_list  = NULL;
        bool         db_dirty    = false;

        pkg_list    = pkg_nodes_alloc_reference();
        update_list = pkg_nodes_alloc_reference();
        build_list  = pkg_nodes_alloc_reference();

        pkg_options.revdeps  = true;
        pkg_options.deep     = true;
        pkg_options.max_dist = -1;

        if (pkg_options.installed_revdeps) {
                rc = pkg_load_installed_deps(slack_pkg_dbi, pkg_graph, pkg_options);
        } else {
                rc = pkg_load_all_deps(pkg_graph, pkg_options);
        }
        if (rc != 0)
                return rc;

        /*
          Load any meta package dependency files
         */
        for (size_t i = 0; i < string_list_size(pkg_names); ++i) {
                const char *pkg_name = string_list_get_const(pkg_names, i);
                if (pkg_is_meta(pkg_name)) {
                        assert(pkg_load_dep(pkg_graph, pkg_name, pkg_options) == 0);
                }
        }

        rc = select_updated_pkgs(slack_pkg_dbi, pkg_graph, pkg_names, pkg_list);
        if (rc != 0) {
                goto finish;
        }

        rc = process_update(slack_pkg_dbi, pkg_graph, pkg_options, pkg_list, update_list, build_list);
        if (rc != 0) {
                goto finish;
        }

        if (pkg_list)
                pkg_nodes_free(&pkg_list);
        if (update_list)
                pkg_nodes_free(&update_list);

        size_t i = 0;
        while (i < pkg_nodes_size(build_list)) {
                int i_incr = 1;

                const struct pkg_node *node = pkg_nodes_get_const(build_list, i);

                assert(PKG_UPDATE_NONE != node->pkg.update.type);

                switch (node->pkg.update.type) {
                case PKG_UPDATE:
                        mesg_ok_label("%4s", " %-24s %-28s %-8s --> %s\n", "[ U]", node->pkg.name, "",
                                      node->pkg.update.version, node->pkg.version);
                        break;
                case PKG_DEP_UPDATE:
                        mesg_ok_label("%4s", " %-24s (P:%-24s) %-8s --> %s\n", "[DU]", node->pkg.name,
                                      node->pkg.update.rel_node->pkg.name, node->pkg.update.version,
                                      node->pkg.version);
                        break;
                case PKG_DEP_REBUILD:
                        mesg_info_label("%4s", " %-24s (P:%-24s) %-8s\n", "[DR]", node->pkg.name,
                                        node->pkg.update.rel_node->pkg.name, node->pkg.version);
                        break;
                case PKG_DEP_DOWNGRADE:
                        mesg_error_label("%4s", " %-24s (P:%-24s) %-8s\n", "[DD]", node->pkg.name,
                                         node->pkg.update.rel_node->pkg.name, node->pkg.version);
                        pkg_nodes_remove(build_list, node->pkg.name);
                        i_incr = 0;
                        break;
                case PKG_DEP_ADDED:
                        mesg_warn_label("%4s", " %-24s (P:%-24s) %-8s\n", "[DA]", node->pkg.name,
                                        node->pkg.update.rel_node->pkg.name, node->pkg.version);
                        break;
                case PKG_REVDEP_UPDATE:
                        mesg_ok_label("%4s", " %-24s (D:%-24s) %-8s --> %s\n", "[PU]", node->pkg.name,
                                      node->pkg.update.rel_node->pkg.name, node->pkg.update.version,
                                      node->pkg.version);
                        break;
                case PKG_REVDEP_REBUILD:
                        mesg_info_label("%4s", " %-24s (D:%-24s) %-8s\n", "[PR]", node->pkg.name,
                                        node->pkg.update.rel_node->pkg.name, node->pkg.version);
                        break;
                case PKG_REVDEP_DOWNGRADE:
                        mesg_error_label("%4s", " %-24s (%-24s) %-8s\n", "[PD]", node->pkg.name,
                                         node->pkg.update.rel_node->pkg.name, node->pkg.version);
                        pkg_nodes_remove(build_list, node->pkg.name);
                        i_incr = 0;
                        break;
                default:
                        abort();
                }
                i += i_incr;
        }

        // 5. Write the sqf file with the package list
        pkg_options.review_type     = PKG_REVIEW_DISABLED;
        pkg_options.check_installed = 0;
        pkg_options.revdeps         = false;
        pkg_options.deep            = false;
        pkg_options.max_dist        = 0;
        pkg_options.recursive       = false;

        rc = write_nodes_sqf(slack_pkg_dbi, pkg_graph, build_list, get_output_path(pkg_options, pkg_names),
                             pkg_options, &db_dirty);

finish:
        if (pkg_list)
                pkg_nodes_free(&pkg_list);
        if (update_list)
                pkg_nodes_free(&update_list);
        if (build_list)
                pkg_nodes_free(&build_list);

        if (db_dirty) {
                rc = pkg_write_db(pkg_graph_sbo_pkgs(pkg_graph));
        }

        return rc;
}
