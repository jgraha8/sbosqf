#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "mesg.h"
#include "options.h"
#include "ostream.h"
#include "pkg.h"
#include "pkg_graph.h"
#include "pkg_ops.h"
#include "pkg_io.h"
#include "slack_pkg_dbi.h"
#include "output_path.h"

void print_build_help()
{
        printf("Usage: %s create [option] pkg\n"
               "Options:\n"
               "  -a, --auto-review\n"
               "  -A, --auto-review-verbose\n"
               "  -i, --ignore-review\n"
               "  -c, --check-installed\n"
               "  -C, --check-any-installed\n"
               "  -d, --deep\n"
               "  -h, --help\n"
               "  -l, --list\n"
               "  -L, --list-slackpkg {1|2}\n"
               "  -o, --output\n"
               "  -n, --no-recursive\n"
               "  -p, --revdeps\n"
               "  -P, --installed-revdeps\n"
               "  -R, --repo-db\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

int process_build_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *        options_str    = "aAibcCdhlL:o:nPpRtT";
        static const struct option long_options[] = {                              /* These options set a flag. */
                                                     LONG_OPT("auto-review", 'a'), /* option */
                                                     LONG_OPT("auto-review-verbose", 'A'), /* option */
                                                     LONG_OPT("ignore-review", 'i'),       /* option */
                                                     LONG_OPT("check-installed", 'c'),     /* option */
                                                     LONG_OPT("check-any-installed", 'C'), /* option */
                                                     LONG_OPT("deep", 'd'),                /* option */
                                                     LONG_OPT("help", 'h'),
                                                     LONG_OPT("list", 'l'),
                                                     LONG_OPT("list-slackpkg", 'L'),
                                                     LONG_OPT("output", 'o'),
                                                     LONG_OPT("no-recursive", 'n'),      /* option */
                                                     LONG_OPT("installed-revdeps", 'P'), /* option */
                                                     LONG_OPT("revdeps", 'p'),           /* option */
                                                     LONG_OPT("repo-db", 'R'),
                                                     {0, 0, 0, 0}};

        int rc = process_options(argc, argv, options_str, long_options, print_build_help, options);

        if (rc >= 0) {
                if (options->output_mode != PKG_OUTPUT_FILE && options->output_name) {
                        mesg_error(
                            "options --list/-l, --list-slackpkg/-L, and --output/-o are mutually exclusive\n");
                        return -1;
                }
        }

        return rc;
}

static int __write_pkg_sqf(const struct slack_pkg_dbi *slack_pkg_dbi,
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

int run_build_command(const struct slack_pkg_dbi *slack_pkg_dbi,
                      struct pkg_graph *          pkg_graph,
                      string_list_t *             pkg_names,
                      struct pkg_options          pkg_options)
{
        int rc = 0;

        struct ostream *os       = NULL;
        bool            db_dirty = false;
        const size_t    num_pkgs = string_list_size(pkg_names);

        for (size_t i = 0; i < num_pkgs; ++i) {
                rc = pkg_load_dep(pkg_graph, string_list_get_const(pkg_names, i), pkg_options);
                if (rc != 0)
                        return rc;
        }

        if (pkg_options.revdeps) {
                if (pkg_options.installed_revdeps) {
                        rc = pkg_load_installed_deps(slack_pkg_dbi, pkg_graph, pkg_options);
                } else {
                        rc = pkg_load_all_deps(pkg_graph, pkg_options);
                }
                if (rc != 0)
                        return rc;
        }

        rc = __write_pkg_sqf(slack_pkg_dbi, pkg_graph, pkg_names, get_output_path(pkg_options, pkg_names),
                             pkg_options, &db_dirty);
        if (rc < 0) {
                goto finish;
        }

        if (db_dirty) {
                rc = pkg_write_db(pkg_graph_sbo_pkgs(pkg_graph));
        }
finish:
        if (os)
                ostream_close(os);

        return rc;
}
