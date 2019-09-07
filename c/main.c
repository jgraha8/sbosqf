/*
 * Copyright (C) 2018-2019 Jason Graham <jgraham@compukix.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 * @brief Main program file for sbopkg-dep2sqf
 *
 */

#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libbds/bds_queue.h>
#include <libbds/bds_stack.h>
#include <libbds/bds_vector.h>

#include "config.h"
#include "pkg_graph.h"
#include "pkg_ops.h"
#include "pkg_util.h"
#include "sbo.h"
#include "slack_pkg.h"
#include "user_config.h"

#define LONG_OPT(long_opt, opt)                                                                                   \
        {                                                                                                         \
                long_opt, no_argument, 0, opt                                                                     \
        }

enum output_mode {
        OUTPUT_STDOUT = 1,
        OUTPUT_FILE
};

static bool pkg_name_required = true;
static bool pkg_name_optional = false;
static enum output_mode output_mode = OUTPUT_FILE;
static bool create_graph = true;

enum action {
        ACTION_NONE,
        ACTION_CHECK_UPDATES,
        ACTION_UPDATEDB,
        ACTION_REVIEW,
        ACTION_SHOW_INFO,
        ACTION_SEARCH_PKG,
        ACTION_EDIT_DEP,
        ACTION_HELP,
        ACTION_WRITE_REMOVE_SQF,
        ACTION_WRITE_SQF,
        ACTION_WRITE_UPDATE_SQF,
};

struct action_struct
{
        enum action action;
        const struct option *option;
};

void set_action(struct action_struct *as, enum action value, const struct option *option)
{
        assert(option);

        if (as->action != ACTION_NONE && as->option != option) {

                fprintf(stderr, "argument --%s/-%c conflicts with argument --%s/-%c\n", as->option->name,
                        as->option->val, option->name, option->val);
                exit(EXIT_FAILURE);
        }
        as->action = value;
        as->option = option;
}

static void print_help();
static int check_updates(struct pkg_graph *pkg_graph, const char *pkg_name);
static int update_pkgdb(struct pkg_graph *pkg_graph);
static int edit_pkg_dep(struct pkg_graph *pkg_graph, const char *pkg_name);
static int write_pkg_sqf(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options pkg_options,
                         enum output_mode output_mode);
static int write_pkg_update_sqf(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options pkg_options,
                                enum output_mode output_mode);
static int write_pkg_remove_sqf(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options pkg_options,
                                enum output_mode output_mode);
static int review_pkg(struct pkg_graph *pkg_graph, const char *pkg_name);
static int show_pkg_info(struct pkg_graph *pkg_graph, const char *pkg_name);
static int search_pkg(const pkg_nodes_t *sbo_pkgs, const char *pkg_name);

static int process_options(int argc, char **argv, const char *options_str, const struct option *long_options,
                           void (*__print_help)(void), struct pkg_options *pkg_options)
{
        while (1) {
                int option_index = 0;
                char c = getopt_long(argc, argv, options_str, long_options, &option_index);

                if (c == -1) break;

                switch (c) {
                case 'a':
                        pkg_options->auto_review = PKG_AUTO_REVIEW;
                        break;
                case 'A':
                        pkg_options->auto_review = PKG_AUTO_REVIEW_VERBOSE;
                        break;
                case 'c':
                        pkg_options->check_installed |= PKG_CHECK_INSTALLED;
                        break;
                case 'd':
                        pkg_options->deep = true;
                        break;
                case 'g':
                        pkg_options->graph = true;
                        break;
                case 'h':
                        __print_help();
                        exit(0);
                case 'k':
                        pkg_options->check_installed |= PKG_CHECK_ANY_INSTALLED;
                        break;
                case 'n':
                        pkg_options->recursive = false;
                        break;
                case 'p':
                        pkg_options->revdeps = true;
                        break;
                default:
                        abort();
                }
        }

        return optind;
}

static void cmd_create_print_help()
{
        printf("Usage: %s create [option] pkg\n"
               "Options:\n"
               "  -a, --auto-review\n"
               "  -A, --auto-review-verbose\n"
               "  -c, --check-installed\n"
               "  -d, --deep\n"
               "  -h, --help\n"
               "  -k, --check-any-installed\n"
               "  -n, --no-recursive\n"
               "  -p, --revdeps\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

static int cmd_create_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *options_str = "aAcdhknp";
        static const struct option long_options[] = {
                /* These options set a flag. */
                LONG_OPT("auto-review", 'a'),         /* option */
                LONG_OPT("auto-review-verbose", 'A'), /* option */
                LONG_OPT("check-installed", 'c'),     /* option */
                LONG_OPT("deep", 'd'),                /* option */
                LONG_OPT("help", 'h'),
                LONG_OPT("check-any-installed", 'k'), /* option */
                LONG_OPT("no-recursive", 'n'),        /* option */
                LONG_OPT("revdeps", 'p'),             /* option */
                { 0, 0, 0, 0 }
        };

        return process_options(argc, argv, options_str, long_options, cmd_create_print_help, options);
}

static void cmd_edit_print_help()
{
        printf("Usage: %s edit [option] pkg\n"
               "Options:\n"
               "  -h, --help\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

static int cmd_edit_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *options_str = "h";
        static const struct option long_options[] = { LONG_OPT("help", 'h'), { 0, 0, 0, 0 } };

        return process_options(argc, argv, options_str, long_options, cmd_edit_print_help, options);
}

int main(int argc, char **argv)
{
        struct pkg_graph *pkg_graph = NULL;
        pkg_nodes_t *sbo_pkgs = NULL;
        struct pkg_options pkg_options = pkg_options_default();

        if (setvbuf(stdout, NULL, _IONBF, 0) != 0) perror("setvbuf()");

        user_config_init();

        struct action_struct as = { ACTION_NONE, NULL };

        /*
          Get command
         */
        if (argc < 2) {
                fprintf(stderr, "no command provided\n");
                exit(EXIT_FAILURE);
        }

        {
                int num_opts = 0;
                --argc;
                ++argv;

                if (strcmp(argv[0], "create") == 0) {
                        as.action = ACTION_WRITE_SQF;
                        num_opts = cmd_create_options(argc, argv, &pkg_options);
                } else if (strcmp(argv[0], "list") == 0) {
                        as.action = ACTION_WRITE_SQF;
                        output_mode = OUTPUT_STDOUT;
                        num_opts = cmd_create_options(argc, argv, &pkg_options);
                } else if (strcmp(argv[0], "edit") == 0) {
                        as.action = ACTION_EDIT_DEP;
                        num_opts = cmd_edit_options(argc, argv, &pkg_options);
                } else if (strcmp(argv[0], "help") == 0) {
                        print_help();
                        exit(EXIT_SUCCESS);
                } else if (strcmp(argv[0], "review") == 0) {
                        as.action = ACTION_REVIEW;
                        // cmd_review_options();
                } else if (strcmp(argv[0], "updatedb") == 0) {
                        as.action = ACTION_UPDATEDB;
                        pkg_name_required = false;
                        // cmd_updatedb_options();
                } else if (strcmp(argv[0], "check-updates") == 0) {
                        as.action = ACTION_CHECK_UPDATES;
                        pkg_name_required = false;
                        pkg_name_optional = true;
                } else {
                        fprintf(stderr, "incorrect command provided: %s\n", argv[0]);
                        exit(EXIT_FAILURE);
                }

                argc -= num_opts;
                argv += num_opts;
        }

        const char *pkg_name = NULL;

        if (pkg_name_required) {
                if (argc != 1) {
                        print_help();
                        exit(EXIT_FAILURE);
                }
                pkg_name = argv[0];
        } else {
                if (pkg_name_optional) {
                        if (argc == 1) {
                                pkg_name = argv[0];
                        } else if (argc > 1) {
                                print_help();
                                exit(EXIT_FAILURE);
                        }
                }
        }

        pkg_graph = pkg_graph_alloc();
        sbo_pkgs = pkg_graph_sbo_pkgs(pkg_graph);

        if (create_graph) {
                if (!pkg_db_exists()) {
                        pkg_load_sbo(sbo_pkgs);
                        pkg_write_db(sbo_pkgs);
                        pkg_create_default_deps(sbo_pkgs);
                } else {
                        pkg_load_db(sbo_pkgs);
                }
        }

        int rc = 0;

        if (as.action == 0) {
                as.action = ACTION_WRITE_SQF;
        }

        switch (as.action) {
        case ACTION_REVIEW:
                rc = review_pkg(pkg_graph, pkg_name);
                break;
        case ACTION_SHOW_INFO:
                rc = show_pkg_info(pkg_graph, pkg_name);
                break;
        case ACTION_CHECK_UPDATES:
                rc = check_updates(pkg_graph, pkg_name);
                break;
        case ACTION_WRITE_UPDATE_SQF:
                rc = write_pkg_update_sqf(pkg_graph, pkg_name, pkg_options, output_mode);
                break;
        case ACTION_UPDATEDB:
                rc = update_pkgdb(pkg_graph);
                break;
        case ACTION_EDIT_DEP:
                rc = edit_pkg_dep(pkg_graph, pkg_name);
                break;
        case ACTION_WRITE_SQF:
                rc = write_pkg_sqf(pkg_graph, pkg_name, pkg_options, output_mode);
                break;
        case ACTION_WRITE_REMOVE_SQF:
                rc = write_pkg_remove_sqf(pkg_graph, pkg_name, pkg_options, output_mode);
                break;
        case ACTION_SEARCH_PKG:
                rc = search_pkg(sbo_pkgs, pkg_name);
                break;
        default:
                printf("action %d not handled\n", as.action);
        }

        if (pkg_graph) {
                pkg_graph_free(&pkg_graph);
        }

        user_config_destroy();

        return rc;
}

static void print_help()
{
        printf("Usage: %s command [options] [args]\n"
               "Commands:\n"
               "  create [options] pkg       create pkg sqf file\n"
               "  list [options] pkg         list pkg dependencies\n"
               "  updatedb                   update the package database\n"
               "  check-updates [pkg]        check for updates\n"
               "  help                       show this information\n"
               "  review pkg                 review package and depenency file information\n"
               "  edit pkg                   edit package dependency file\n",
               PROGRAM_NAME);
}

enum updated_pkg_status {
        PKG_UPDATED,
        PKG_DOWNGRADED,
        PKG_REMOVED,
};

struct updated_pkg
{
        enum updated_pkg_status status;
        const struct pkg_node *node;
        char *name;
        char *slack_pkg_version;
        char *sbo_version;
};

static void destroy_updated_pkg(struct updated_pkg *updated_pkg)
{
        if (updated_pkg->name) free(updated_pkg->name);
        if (updated_pkg->slack_pkg_version) free(updated_pkg->slack_pkg_version);
        if (updated_pkg->sbo_version) free(updated_pkg->sbo_version);

        memset(updated_pkg, 0, sizeof(*updated_pkg));
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static int get_updated_pkgs(struct pkg_graph *pkg_graph, const char *pkg_name, struct bds_queue *updated_pkg_queue)
{
        bool have_pkg = false; /* Used only for single package pkg_name */
        pkg_nodes_t *pkg_nodes = NULL;

        if (pkg_name) {
                pkg_nodes = pkg_nodes_alloc_reference();
                struct pkg_node *node = pkg_graph_search(pkg_graph, pkg_name);
                if (node == NULL) goto finish;

                pkg_nodes_append(pkg_nodes, node);
        } else {
                pkg_nodes = pkg_graph_sbo_pkgs(pkg_graph);
        }

        const ssize_t num_slack_pkgs = slack_pkg_size();
        if (num_slack_pkgs < 0) return 1;

        for (ssize_t i = 0; i < num_slack_pkgs; ++i) {

                struct updated_pkg updated_pkg;

                const struct slack_pkg *slack_pkg = slack_pkg_get_const((size_t)i, user_config.sbo_tag);
                if (slack_pkg == NULL) continue; /* tag did not match */

                const struct pkg_node *node = pkg_nodes_bsearch_const(pkg_nodes, slack_pkg->name);
                if (node == NULL) {
                        if (pkg_name == NULL) { /* the package has been removed */
                                updated_pkg.status = PKG_REMOVED;
                                updated_pkg.node = NULL;
                                updated_pkg.name = bds_string_dup(slack_pkg->name);
                                updated_pkg.slack_pkg_version = bds_string_dup(slack_pkg->version);
                                updated_pkg.sbo_version = NULL;
                                bds_queue_push(updated_pkg_queue, &updated_pkg);
                        }
                        continue;
                }

                if (pkg_name) have_pkg = true;

                const char *sbo_version = sbo_read_version(node->pkg.sbo_dir, node->pkg.name);
                assert(sbo_version);

                int diff = compar_versions(slack_pkg->version, sbo_version);
                if (diff == 0) continue;

                updated_pkg.status = (diff < 0 ? PKG_UPDATED : PKG_DOWNGRADED);
                updated_pkg.node = node;
                updated_pkg.name = bds_string_dup(node->pkg.name);
                updated_pkg.slack_pkg_version = bds_string_dup(slack_pkg->version);
                updated_pkg.sbo_version = bds_string_dup(sbo_version);
                bds_queue_push(updated_pkg_queue, &updated_pkg);
        }
finish:
        if (pkg_name) {
                if (!have_pkg) {
                        /* Check if it's installed */
                        const struct slack_pkg *slack_pkg = slack_pkg_search_const(pkg_name, user_config.sbo_tag);
                        if (slack_pkg) {
                                struct updated_pkg updated_pkg;
                                updated_pkg.status = PKG_REMOVED;
                                updated_pkg.node = NULL;
                                updated_pkg.name = bds_string_dup(slack_pkg->name);
                                updated_pkg.slack_pkg_version = bds_string_dup(slack_pkg->version);
                                updated_pkg.sbo_version = NULL;
                                bds_queue_push(updated_pkg_queue, &updated_pkg);
                        }
                }
                pkg_nodes_free(&pkg_nodes);
        }
        return 0;
}

static int check_updates(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        int rc = 0;

        struct bds_queue *updated_pkg_queue =
            bds_queue_alloc(1, sizeof(struct updated_pkg), (void (*)(void *))destroy_updated_pkg);

        bds_queue_set_autoresize(updated_pkg_queue, true);

        if ((rc = get_updated_pkgs(pkg_graph, pkg_name, updated_pkg_queue)) != 0) goto finish;

        struct updated_pkg updated_pkg;

        while (bds_queue_pop(updated_pkg_queue, &updated_pkg)) {
                switch (updated_pkg.status) {
                case PKG_UPDATED:
                        printf(COLOR_OK "%4s" COLOR_END " %-24s %-8s --> %s\n", "[U]", updated_pkg.name,
                               updated_pkg.slack_pkg_version, updated_pkg.sbo_version);
                        break;
                case PKG_DOWNGRADED:
                        printf(COLOR_INFO "%4s" COLOR_END " %-24s %-8s --> %s\n", "[D]", updated_pkg.name,
                               updated_pkg.slack_pkg_version, updated_pkg.sbo_version);
                        break;
                default: /* PKG_REMOVED */
                        printf(COLOR_FAIL "%4s" COLOR_END " %-24s %-8s\n", "[R]", updated_pkg.name,
                               updated_pkg.slack_pkg_version);
                }
                destroy_updated_pkg(&updated_pkg);
        }

finish:
        if (updated_pkg_queue) bds_queue_free(&updated_pkg_queue);

        return rc;
}

static int update_pkgdb(struct pkg_graph *pkg_graph)
{
        int rc = 0;
        pkg_nodes_t *sbo_pkgs = pkg_graph_sbo_pkgs(pkg_graph);
        pkg_nodes_t *new_sbo_pkgs = pkg_nodes_alloc();

        if ((rc = pkg_load_sbo(new_sbo_pkgs)) != 0) return rc;

        rc = pkg_compar_sets(new_sbo_pkgs, sbo_pkgs);
        sbo_pkgs = pkg_graph_assign_sbo_pkgs(pkg_graph, new_sbo_pkgs);

        if (rc != 0) return rc;

        if ((rc = pkg_write_db(sbo_pkgs)) != 0) return rc;

        return pkg_create_default_deps(sbo_pkgs);
}

static int review_pkg(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        int rc = 0;
        pkg_nodes_t *reviewed_pkgs = NULL;
        bool reviewed_pkgs_dirty = false;

        struct pkg_node *reviewed_node = NULL;
        const struct pkg_node *pkg_node = NULL;

        struct pkg_options pkg_options = pkg_options_default();

        pkg_options.recursive = false;

        rc = pkg_load_dep(pkg_graph, pkg_name, pkg_options);
        if (rc != 0) return rc;

        pkg_node = (const struct pkg_node *)pkg_graph_search(pkg_graph, pkg_name);
        if (pkg_node == NULL) {
                fprintf(stderr, "package %s does not exist\n", pkg_name);
                rc = 1;
                return rc;
        }

        reviewed_pkgs = pkg_nodes_alloc();
        if (pkg_reviewed_exists()) {
                rc = pkg_load_reviewed(reviewed_pkgs);
                if (rc != 0) goto finish;
        }

        reviewed_node = pkg_nodes_bsearch(reviewed_pkgs, pkg_name);

        if (reviewed_node && reviewed_node->pkg.info_crc == pkg_node->pkg.info_crc) {
                rc = pkg_review(&pkg_node->pkg);
        } else {
                int dep_status;
                rc = pkg_review_prompt(&pkg_node->pkg, 0, &dep_status);
                if (rc == 0) {
                        if (reviewed_node) {
                                if (reviewed_node->pkg.info_crc != pkg_node->pkg.info_crc) {
                                        pkg_set_version(&reviewed_node->pkg, pkg_node->pkg.version);
                                        reviewed_node->pkg.info_crc = pkg_node->pkg.info_crc;
                                        reviewed_pkgs_dirty = true;
                                }
                        } else {
                                reviewed_node = pkg_node_alloc(pkg_node->pkg.name);
                                pkg_init_version(&reviewed_node->pkg, pkg_node->pkg.version);
                                reviewed_node->pkg.info_crc = pkg_node->pkg.info_crc;
                                pkg_nodes_insert_sort(reviewed_pkgs, reviewed_node);
                                reviewed_pkgs_dirty = true;
                        }
                        if (reviewed_pkgs_dirty) rc = pkg_write_reviewed(reviewed_pkgs);
                }
        }
finish:
        if (reviewed_pkgs) pkg_nodes_free(&reviewed_pkgs);

        return rc;
}

static int show_pkg_info(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        const struct pkg_node *pkg_node = NULL;

        pkg_node = (const struct pkg_node *)pkg_graph_search(pkg_graph, pkg_name);
        if (pkg_node == NULL) {
                fprintf(stderr, "package %s does not exist\n", pkg_name);
                return 1;
        }
        return pkg_show_info(&pkg_node->pkg);
}

static int edit_pkg_dep(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        const struct pkg_node *pkg_node = NULL;

        pkg_node = (const struct pkg_node *)pkg_graph_search(pkg_graph, pkg_name);
        if (pkg_node == NULL) {
                fprintf(stderr, "package %s does not exist\n", pkg_name);
                return 1;
        }
        return edit_dep_file(pkg_node->pkg.name);
}

static int write_pkg_sqf(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options pkg_options,
                         enum output_mode output_mode)
{
        int rc = 0;
        char sqf_file[256];
        struct ostream *os = NULL;

        pkg_nodes_t *reviewed_pkgs = NULL;
        bool reviewed_pkgs_dirty = false;

        rc = pkg_load_dep(pkg_graph, pkg_name, pkg_options);
        if (rc != 0) return rc;

        if (pkg_options.revdeps) {
                rc = pkg_load_all_deps(pkg_graph, pkg_options);
                if (rc != 0) return rc;
        }

        if (pkg_options.revdeps) {
                bds_string_copyf(sqf_file, sizeof(sqf_file), "%s-revdeps.sqf", pkg_name);
        } else {
                bds_string_copyf(sqf_file, sizeof(sqf_file), "%s.sqf", pkg_name);
        }

        reviewed_pkgs = pkg_nodes_alloc();
        if (pkg_reviewed_exists()) {
                rc = pkg_load_reviewed(reviewed_pkgs);
                if (rc != 0) goto finish;
        }

        while (1) {

                bool buffer_stream = (output_mode != OUTPUT_FILE);
                const char *output_path = (output_mode == OUTPUT_FILE ? &sqf_file[0] : "/dev/stdout");
                os = ostream_open(output_path, "w", buffer_stream);

                if (os == NULL) {
                        fprintf(stderr, "unable to create %s\n", output_path);
                        rc = 1;
                        goto finish;
                }
                rc = write_sqf(os, pkg_graph, pkg_name, pkg_options, reviewed_pkgs, &reviewed_pkgs_dirty);

                if (rc > 0) {
                        /* A dependency file was modified during review */
                        ostream_close(os);
                        continue;
                } else if (rc < 0) {
                        goto finish;
                }

                break;
        }

        if (reviewed_pkgs_dirty) {
                rc = pkg_write_reviewed(reviewed_pkgs);
        }

finish:
        if (reviewed_pkgs) pkg_nodes_free(&reviewed_pkgs);

        ostream_close(os);

        return rc;
}

static int write_pkg_update_sqf(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options pkg_options,
                                enum output_mode output_mode)
{
        return 0;
}

static int write_pkg_remove_sqf(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options pkg_options,
                                enum output_mode output_mode)
{
        int rc = 0;
        char sqf_file[256];
        FILE *fp = NULL;
        const char *term = NULL;
        struct bds_queue *pkg_list = NULL;
        struct bds_stack *removal_list = NULL;
        struct pkg_node *node = NULL;
        struct pkg_iterator iter;
        struct pkg_options options = pkg_options_default();
        pkg_iterator_flags_t flags = 0;
        int max_dist = 0;

        options.revdeps = true;

        if ((rc = pkg_load_all_deps(pkg_graph, options)) != 0) return rc;
        if ((rc = pkg_load_dep(pkg_graph, pkg_name, options)) != 0) return rc;

        pkg_list = bds_queue_alloc(1, sizeof(struct pkg_node *), NULL);
        bds_queue_set_autoresize(pkg_list, true);

        removal_list = bds_stack_alloc(1, sizeof(struct pkg_node *), NULL);

        flags = PKG_ITER_DEPS | PKG_ITER_FORW;
        max_dist = (pkg_options.deep ? -1 : 0);
        for (node = pkg_iterator_begin(&iter, pkg_graph, pkg_name, flags, max_dist); node != NULL;
             node = pkg_iterator_next(&iter)) {

                if (node->pkg.dep.is_meta) continue;

                node->pkg.for_removal = true;
                bds_queue_push(pkg_list, &node);
        }
        pkg_iterator_destroy(&iter);

        while (bds_queue_pop(pkg_list, &node)) {

                flags = PKG_ITER_REVDEPS;
                max_dist = 1;

                for (struct pkg_node *parent_node =
                         pkg_iterator_begin(&iter, pkg_graph, node->pkg.name, flags, max_dist);
                     parent_node != NULL; parent_node = pkg_iterator_next(&iter)) {

                        if (strcmp(parent_node->pkg.name, node->pkg.name) == 0) continue;

                        if (parent_node->pkg.dep.is_meta) continue;

                        if (!parent_node->pkg.for_removal &&
                            slack_pkg_is_installed(parent_node->pkg.name, user_config.sbo_tag)) {
                                printf(COLOR_FAIL "%12s" COLOR_END " %-24s <-- %s\n", "[required]", node->pkg.name,
                                       parent_node->pkg.name);
                                node->pkg.for_removal = false;
                                break;
                        }
                }
                pkg_iterator_destroy(&iter);

                if (node->pkg.for_removal) bds_stack_push(removal_list, &node);
        }

        if (bds_stack_size(removal_list) == 0) goto finish;

        bds_string_copyf(sqf_file, sizeof(sqf_file), "%s-remove.sqf", pkg_name);

        if (output_mode == OUTPUT_FILE) {
                fp = fopen(sqf_file, "w");
                if (fp == NULL) {
                        fprintf(stderr, "unable to create %s\n", sqf_file);
                        rc = 1;
                        goto finish;
                }
                term = "\n";
        } else {
                fp = stdout;
                term = " ";
        }

        while (bds_stack_pop(removal_list, &node)) {
                fprintf(fp, "%s%s", node->pkg.name, term);
        }

        if (fp != stdout) {
                printf("created %s\n", sqf_file);
        } else {
                fprintf(fp, "\n");
        }

finish:
        if (fp && fp != stdout) fclose(fp);

        if (pkg_list) bds_queue_free(&pkg_list);
        if (removal_list) bds_stack_free(&removal_list);

        return rc;
}

static int compar_string_ptr(const void *a, const void *b)
{
        return strcmp(*(const char **)a, *(const char **)b);
}
static int search_pkg(const pkg_nodes_t *sbo_pkgs, const char *pkg_name)
{
        size_t num_nodes = 0;
        struct bds_vector *results = NULL;
        size_t num_results = 0;
        char *__pkg_name = NULL;
        const size_t sbo_dir_offset = strlen(user_config.sbopkg_repo) + 1;

        __pkg_name = bds_string_dup(pkg_name);
        bds_string_tolower(__pkg_name);

        results = bds_vector_alloc(1, sizeof(const char *), free);
        num_nodes = pkg_nodes_size(sbo_pkgs);
        for (size_t i = 0; i < num_nodes; ++i) {
                const struct pkg_node *node = pkg_nodes_get_const(sbo_pkgs, i);
                char *sbo_dir = bds_string_dup(node->pkg.sbo_dir + sbo_dir_offset);
                char *p = bds_string_dup(node->pkg.name);
                if (bds_string_contains(bds_string_tolower(p), __pkg_name)) {
                        bds_vector_append(results, &sbo_dir);
                }
                free(p);
        }
        free(__pkg_name);

        bds_vector_qsort(results, compar_string_ptr);

        num_results = bds_vector_size(results);
        for (size_t i = 0; i < num_results; ++i) {
                printf("%s\n", *(const char **)bds_vector_get(results, i));
        }

        return 0;
}
