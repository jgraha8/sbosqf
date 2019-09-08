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
#include "mesg.h"
#include "pkg_graph.h"
#include "pkg_ops.h"
#include "pkg_util.h"
#include "sbo.h"
#include "slack_pkg.h"
#include "string_list.h"
#include "user_config.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define LONG_OPT(long_opt, opt)                                                                                   \
        {                                                                                                         \
                long_opt, no_argument, 0, opt                                                                     \
        }

static bool pkg_name_required  = true;
static bool pkg_name_optional  = false;
static bool multiple_pkg_names = false;
static bool create_graph       = true;

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
        ACTION_MAKE_META,
};

struct action_struct {
        enum action action;
        const struct option *option;
};

void set_action(struct action_struct *as, enum action value, const struct option *option)
{
        assert(option);

        if (as->action != ACTION_NONE && as->option != option) {

                mesg_error("argument --%s/-%c conflicts with argument --%s/-%c\n", as->option->name,
                           as->option->val, option->name, option->val);
                exit(EXIT_FAILURE);
        }
        as->action = value;
        as->option = option;
}

static const struct option *find_option(const struct option *long_options, const char *long_name, const int name)
{
        assert(long_name || name);

        const struct option *opt = long_options;
        while (opt->name) {
                if (long_name) {
                        if (strcmp(opt->name, long_name) == 0) {
                                return opt;
                        }
                } else {
                        if (name == opt->val) {
                                return opt;
                        }
                }
                ++opt;
        }

        return NULL;
}

/*
 * Sets the package review type and checks for conflicting pre-existing settings.
 *
 * It is used to check for conflicting settings for the review type. It is based on an
 * increasing priority (0 lowest priority and 3 the highest priority):
 *
 *   0 - PKG_REVIEW_DISABLED
 *   1 - PKG_REVIEW_AUTO
 *   2 - PKG_REVIEW_AUTO_VERBOSE
 *   3 - PKG_REVIEW_ENABLED
 */
struct review_option_prio {
        int prio;
        char optval;
};

#define REVIEW_OPTION_PRIO(__prio_val, __prio_optval)                                                             \
        ({                                                                                                        \
                struct review_option_prio __prio;                                                                 \
                __prio.prio   = __prio_val;                                                                       \
                __prio.optval = __prio_optval;                                                                    \
                __prio;                                                                                           \
        })

static void set_pkg_review_type(enum pkg_review_type *review_type, enum pkg_review_type type_val,
                                const struct option *long_options)
{
        static struct review_option_prio prio[4];
        static bool initd = false;

        const struct option *prev_option = NULL;
        const struct option *option      = NULL;

        if (!initd) {
                prio[PKG_REVIEW_DISABLED]     = REVIEW_OPTION_PRIO(3, 'i');
                prio[PKG_REVIEW_AUTO]         = REVIEW_OPTION_PRIO(1, 'a');
                prio[PKG_REVIEW_AUTO_VERBOSE] = REVIEW_OPTION_PRIO(2, 'A');
                prio[PKG_REVIEW_ENABLED]      = REVIEW_OPTION_PRIO(0, 0);

                initd = true;
        }

        if (prio[*review_type].prio == prio[type_val].prio)
                return;

        if (prio[*review_type].optval && prio[type_val].optval) {
                prev_option = find_option(long_options, NULL, prio[*review_type].optval);
                option      = find_option(long_options, NULL, prio[type_val].optval);

                mesg_warn("warning: option --%s/-%c conflicts with --%s/-%c", prev_option->name, prev_option->val,
                          option->name, option->val);
        }

        if (prio[type_val].prio > prio[*review_type].prio) {
                *review_type = type_val;
                if (option) {
                        fprintf(stderr, ": using option --%s/-%c\n", option->name, option->val);
                }
        } else {
                if (prev_option) {
                        fprintf(stderr, ": using option --%s/-%c\n", prev_option->name, prev_option->val);
                }
        }
}

static void print_help();
static int check_updates(struct pkg_graph *pkg_graph, const char *pkg_name);
static int update_pkgdb(struct pkg_graph *pkg_graph);
static int edit_pkg_dep(struct pkg_graph *pkg_graph, const char *pkg_name);
static int write_pkg_sqf(struct pkg_graph *pkg_graph, string_list_t *pkg_names, struct pkg_options pkg_options);

static int write_pkg_update_sqf(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options pkg_options);

static int write_pkg_remove_sqf(struct pkg_graph *pkg_graph, const string_list_t *pkg_names, struct pkg_options pkg_options);
static int review_pkg(struct pkg_graph *pkg_graph, const char *pkg_name);
static int show_pkg_info(struct pkg_graph *pkg_graph, const char *pkg_name);
static int search_pkg(const pkg_nodes_t *sbo_pkgs, const char *pkg_name);
static int make_meta_pkg(const pkg_nodes_t *sbo_pkgs, const char *meta_pkg_name, string_list_t *pkg_names);

static int process_options(int argc, char **argv, const char *options_str, const struct option *long_options,
                           void (*__print_help)(void), struct pkg_options *pkg_options)
{
        while (1) {
                int option_index = 0;
                char c           = getopt_long(argc, argv, options_str, long_options, &option_index);

                if (c == -1)
                        break;

                switch (c) {
                case 'a':
                        set_pkg_review_type(&pkg_options->review_type, PKG_REVIEW_AUTO, long_options);
                        break;
                case 'A':
                        set_pkg_review_type(&pkg_options->review_type, PKG_REVIEW_AUTO_VERBOSE, long_options);
                        break;
                case 'i':
                        set_pkg_review_type(&pkg_options->review_type, PKG_REVIEW_DISABLED, long_options);
                        break;
                case 'c':
                        pkg_options->check_installed |= PKG_CHECK_INSTALLED;
                        break;
                case 'C':
                        pkg_options->check_installed |= PKG_CHECK_ANY_INSTALLED;
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
                case 'l':
                        pkg_options->output_mode = PKG_OUTPUT_STDOUT;
                        break;
                case 'n':
                        pkg_options->recursive = false;
                        break;
                case 'o':
                        pkg_options->output_name = optarg;
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
               "  -i, --ignore-review\n"
               "  -c, --check-installed\n"
               "  -C, --check-any-installed\n"
               "  -d, --deep\n"
               "  -h, --help\n"
               "  -l, --list\n"
               "  -o, --output\n"
               "  -n, --no-recursive\n"
               "  -p, --revdeps\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

static int cmd_create_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *options_str            = "aAibcCdhlo:np";
        static const struct option long_options[] = {                              /* These options set a flag. */
                                                     LONG_OPT("auto-review", 'a'), /* option */
                                                     LONG_OPT("auto-review-verbose", 'A'), /* option */
                                                     LONG_OPT("ignore-review", 'i'),       /* option */
                                                     LONG_OPT("check-installed", 'c'),     /* option */
                                                     LONG_OPT("check-any-installed", 'C'), /* option */
                                                     LONG_OPT("deep", 'd'),                /* option */
                                                     LONG_OPT("help", 'h'),
                                                     LONG_OPT("list", 'l'),
                                                     LONG_OPT("output", 'o'),
                                                     LONG_OPT("no-recursive", 'n'), /* option */
                                                     LONG_OPT("revdeps", 'p'),      /* option */
                                                     {0, 0, 0, 0}};

        int rc = process_options(argc, argv, options_str, long_options, cmd_create_print_help, options);

        if (rc >= 0) {
                if (options->output_mode != PKG_OUTPUT_FILE && options->output_name) {
                        mesg_error("options --list/-l and --output/-o are mutually exclusive\n");
                        return -1;
                }
        }

        return rc;
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
        static const char *options_str            = "h";
        static const struct option long_options[] = {LONG_OPT("help", 'h'), {0, 0, 0, 0}};

        return process_options(argc, argv, options_str, long_options, cmd_edit_print_help, options);
}

static void cmd_info_print_help()
{
        printf("Usage: %s info [option] pkg\n"
               "Options:\n"
               "  -h, --help\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

static int cmd_info_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *options_str            = "h";
        static const struct option long_options[] = {LONG_OPT("help", 'h'), {0, 0, 0, 0}};

        return process_options(argc, argv, options_str, long_options, cmd_info_print_help, options);
}

static void cmd_review_print_help()
{
        printf("Usage: %s review [option] pkg\n"
               "Options:\n"
               "  -h, --help\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

static int cmd_review_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *options_str            = "h";
        static const struct option long_options[] = {LONG_OPT("help", 'h'), {0, 0, 0, 0}};

        return process_options(argc, argv, options_str, long_options, cmd_review_print_help, options);
}

static void cmd_search_print_help()
{
        printf("Usage: %s search [option] pkg\n"
               "Options:\n"
               "  -h, --help\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

static int cmd_search_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *options_str            = "h";
        static const struct option long_options[] = {LONG_OPT("help", 'h'), {0, 0, 0, 0}};

        return process_options(argc, argv, options_str, long_options, cmd_search_print_help, options);
}

static void cmd_updatedb_print_help()
{
        printf("Usage: %s updatedb [option]\n"
               "Options:\n"
               "  -h, --help\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

static int cmd_updatedb_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *options_str            = "h";
        static const struct option long_options[] = {LONG_OPT("help", 'h'), {0, 0, 0, 0}};

        return process_options(argc, argv, options_str, long_options, cmd_updatedb_print_help, options);
}

static void cmd_check_updates_print_help()
{
        printf("Usage: %s check-updates [option] [pkg]\n"
               "Checks for updated packages.\n"
               "\n"
               "If a package name is not provided, then updates for all packages will be provided.\n"
               "\n"
               "Options:\n"
               "  -h, --help\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

static int cmd_check_updates_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *options_str            = "h";
        static const struct option long_options[] = {LONG_OPT("help", 'h'), {0, 0, 0, 0}};

        return process_options(argc, argv, options_str, long_options, cmd_check_updates_print_help, options);
}

static void cmd_make_meta_print_help()
{
        printf("Usage: %s make-meta -o metapkg [options] pkgs...\n"
               "Creates a meta package from a set of provided packages.\n"
               "\n"
               "NOTE: Option -o is required.\n"
               "\n"
               "Options:\n"
               "  -o, --output metapkg\n"
               "  -h, --help\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

static int cmd_make_meta_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *options_str            = "o:h";
        static const struct option long_options[] = {LONG_OPT("output", 'o'), LONG_OPT("help", 'h'), {0, 0, 0, 0}};

        int rc = process_options(argc, argv, options_str, long_options, cmd_make_meta_print_help, options);

        if (rc == 0) {
                if (options->output_name == NULL) {
                        mesg_error("Output metapkg not provided (option --output/-o\n");
                        rc = 1;
                }
        }

        /* Check that the meta package provided does not already exist as a package in the repo */

        return rc;
}

int main(int argc, char **argv)
{
        int rc = 0;

        struct pkg_graph *pkg_graph    = NULL;
        pkg_nodes_t *sbo_pkgs          = NULL;
        struct pkg_options pkg_options = pkg_options_default();
        string_list_t *pkg_names       = NULL;

        if (setvbuf(stdout, NULL, _IONBF, 0) != 0)
                perror("setvbuf()");

        user_config_init();

        struct action_struct as = {ACTION_NONE, NULL};

        /*
          Get command
         */
        if (argc < 2) {
                mesg_error("no command provided\n");
                exit(EXIT_FAILURE);
        }

        {
                int num_opts = 0;
                --argc;
                ++argv;

                const char *cmd = argv[0];
                if (strcmp(cmd, "create") == 0) {

                        as.action          = ACTION_WRITE_SQF;
                        num_opts           = cmd_create_options(argc, argv, &pkg_options);
                        multiple_pkg_names = true;

                } else if (strcmp(cmd, "edit") == 0) {

                        as.action = ACTION_EDIT_DEP;
                        num_opts  = cmd_edit_options(argc, argv, &pkg_options);

                } else if (strcmp(cmd, "help") == 0) {

                        print_help();
                        exit(EXIT_SUCCESS);

                } else if (strcmp(cmd, "info") == 0) {

                        as.action = ACTION_SHOW_INFO;
                        num_opts  = cmd_info_options(argc, argv, &pkg_options);

                } else if (strcmp(cmd, "review") == 0) {

                        as.action = ACTION_REVIEW;
                        num_opts  = cmd_review_options(argc, argv, &pkg_options);

                } else if (strcmp(cmd, "search") == 0) {

                        as.action = ACTION_SEARCH_PKG;
                        num_opts  = cmd_search_options(argc, argv, &pkg_options);

                } else if (strcmp(cmd, "updatedb") == 0) {

                        as.action         = ACTION_UPDATEDB;
                        pkg_name_required = false;
                        num_opts          = cmd_updatedb_options(argc, argv, &pkg_options);

                } else if (strcmp(cmd, "check-updates") == 0) {

                        as.action         = ACTION_CHECK_UPDATES;
                        pkg_name_required = false;
                        pkg_name_optional = true;
                        num_opts          = cmd_check_updates_options(argc, argv, &pkg_options);

                } else if (strcmp(cmd, "make-meta") == 0) {

                        as.action          = ACTION_MAKE_META;
                        num_opts           = cmd_make_meta_options(argc, argv, &pkg_options);
                        multiple_pkg_names = true;

                } else {

                        mesg_error("incorrect command provided: %s\n", cmd);
                        exit(EXIT_FAILURE);
                }

                if (num_opts < 0) {
                        mesg_error("unable to process command %s\n", cmd);
                        exit(EXIT_FAILURE);
                }

                argc -= MAX(num_opts, 1);
                argv += MAX(num_opts, 1);
        }

        const char *pkg_name = NULL;
        if (pkg_name_required) {
                if (argc == 0) {
                        mesg_error("no package names provided\n");
                        exit(EXIT_FAILURE);
                }
                if (multiple_pkg_names) {
                        pkg_names = string_list_alloc_reference();
                        for (int i = 0; i < argc; ++i) {
                                if (!dep_file_exists(argv[i])) {
                                        mesg_error("dependency file %s does not exist\n");
                                        exit(EXIT_FAILURE);
                                }
                                string_list_append(pkg_names, argv[i]);
                        }
                } else {
                        if (argc != 1) {
                                print_help();
                                exit(EXIT_FAILURE);
                        }
                        pkg_name = argv[0];
                }
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
        sbo_pkgs  = pkg_graph_sbo_pkgs(pkg_graph);

        if (create_graph) {
                if (!pkg_db_exists()) {
                        pkg_load_sbo(sbo_pkgs);
                        pkg_write_db(sbo_pkgs);
                        pkg_create_default_deps(sbo_pkgs);
                } else {
                        pkg_load_db(sbo_pkgs);
                }
        }

        if (as.action == 0) {
                as.action = ACTION_WRITE_SQF;
        }

        switch (as.action) {
        case ACTION_REVIEW:
                rc = review_pkg(pkg_graph, pkg_name);
                if (rc < 0) {
                        mesg_error("unable to review package %s\n", pkg_name);
                } else if (rc > 0) {
                        mesg_warn("package %s not added to REVIEWED\n", pkg_name);
                }
                break;
        case ACTION_SHOW_INFO:
                rc = show_pkg_info(pkg_graph, pkg_name);
                if (rc != 0) {
                        mesg_error("unable to show package %s\n", pkg_name);
                }
                break;
        case ACTION_CHECK_UPDATES:
                rc = check_updates(pkg_graph, pkg_name);
                if (rc != 0) {
                        if (pkg_name) {
                                mesg_error("unable to check updates for package %s\n", pkg_name);
                        } else {
                                mesg_error("unable to check updates\n");
                        }
                }
                break;
        case ACTION_WRITE_UPDATE_SQF:
                rc = write_pkg_update_sqf(pkg_graph, pkg_name, pkg_options);
                break;
        case ACTION_UPDATEDB:
                rc = update_pkgdb(pkg_graph);
                if (rc != 0) {
                        mesg_error("unable to update package database\n");
                }
                break;
        case ACTION_EDIT_DEP:
                rc = edit_pkg_dep(pkg_graph, pkg_name);
                if (rc != 0) {
                        mesg_error("unable to edit package dependency file %s\n", pkg_name);
                }
                break;
        case ACTION_WRITE_SQF:
                rc = write_pkg_sqf(pkg_graph, pkg_names, pkg_options);
                if (rc != 0) {
                        mesg_error("unable to create dependency list for package %s\n", pkg_name);
                }
                break;
        case ACTION_WRITE_REMOVE_SQF:
                rc = write_pkg_remove_sqf(pkg_graph, pkg_names, pkg_options);
                break;
        case ACTION_SEARCH_PKG:
                rc = search_pkg(sbo_pkgs, pkg_name);
                if (rc != 0) {
                        mesg_error("unable to search for package %s\n", pkg_name);
                }
                break;
        case ACTION_MAKE_META:
                rc = make_meta_pkg(sbo_pkgs, pkg_options.output_name, pkg_names);
                if (rc != 0) {
                        mesg_error("unable to make meta-package %s\n", pkg_options.output_name);
                } else {
                        mesg_ok("created meta-package %s\n", pkg_options.output_name);
                }
                break;
        default:
                mesg_warn("action %d not handled\n", as.action);
        }

        if (pkg_names) {
                string_list_free(&pkg_names);
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
               "  create [options] pkg       create package dependency list or file\n"
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

struct updated_pkg {
        enum updated_pkg_status status;
        const struct pkg_node *node;
        char *name;
        char *slack_pkg_version;
        char *sbo_version;
};

static void destroy_updated_pkg(struct updated_pkg *updated_pkg)
{
        if (updated_pkg->name)
                free(updated_pkg->name);
        if (updated_pkg->slack_pkg_version)
                free(updated_pkg->slack_pkg_version);
        if (updated_pkg->sbo_version)
                free(updated_pkg->sbo_version);

        memset(updated_pkg, 0, sizeof(*updated_pkg));
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static int get_updated_pkgs(struct pkg_graph *pkg_graph, const char *pkg_name, struct bds_queue *updated_pkg_queue)
{
        bool have_pkg          = false; /* Used only for single package pkg_name */
        pkg_nodes_t *pkg_nodes = NULL;

        if (pkg_name) {
                pkg_nodes             = pkg_nodes_alloc_reference();
                struct pkg_node *node = pkg_graph_search(pkg_graph, pkg_name);
                if (node == NULL)
                        goto finish;

                pkg_nodes_append(pkg_nodes, node);
        } else {
                pkg_nodes = pkg_graph_sbo_pkgs(pkg_graph);
        }

        const ssize_t num_slack_pkgs = slack_pkg_size();
        if (num_slack_pkgs < 0)
                return 1;

        for (ssize_t i = 0; i < num_slack_pkgs; ++i) {

                struct updated_pkg updated_pkg;

                const struct slack_pkg *slack_pkg = slack_pkg_get_const((size_t)i, user_config.sbo_tag);
                if (slack_pkg == NULL)
                        continue; /* tag did not match */

                const struct pkg_node *node = pkg_nodes_bsearch_const(pkg_nodes, slack_pkg->name);
                if (node == NULL) {
                        if (pkg_name == NULL) { /* the package has been removed */
                                updated_pkg.status            = PKG_REMOVED;
                                updated_pkg.node              = NULL;
                                updated_pkg.name              = bds_string_dup(slack_pkg->name);
                                updated_pkg.slack_pkg_version = bds_string_dup(slack_pkg->version);
                                updated_pkg.sbo_version       = NULL;
                                bds_queue_push(updated_pkg_queue, &updated_pkg);
                        }
                        continue;
                }

                if (pkg_name)
                        have_pkg = true;

                const char *sbo_version = sbo_read_version(node->pkg.sbo_dir, node->pkg.name);
                assert(sbo_version);

                int diff = compar_versions(slack_pkg->version, sbo_version);
                if (diff == 0)
                        continue;

                updated_pkg.status            = (diff < 0 ? PKG_UPDATED : PKG_DOWNGRADED);
                updated_pkg.node              = node;
                updated_pkg.name              = bds_string_dup(node->pkg.name);
                updated_pkg.slack_pkg_version = bds_string_dup(slack_pkg->version);
                updated_pkg.sbo_version       = bds_string_dup(sbo_version);
                bds_queue_push(updated_pkg_queue, &updated_pkg);
        }
finish:
        if (pkg_name) {
                if (!have_pkg) {
                        /* Check if it's installed */
                        const struct slack_pkg *slack_pkg = slack_pkg_search_const(pkg_name, user_config.sbo_tag);
                        if (slack_pkg) {
                                struct updated_pkg updated_pkg;
                                updated_pkg.status            = PKG_REMOVED;
                                updated_pkg.node              = NULL;
                                updated_pkg.name              = bds_string_dup(slack_pkg->name);
                                updated_pkg.slack_pkg_version = bds_string_dup(slack_pkg->version);
                                updated_pkg.sbo_version       = NULL;
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

        if ((rc = get_updated_pkgs(pkg_graph, pkg_name, updated_pkg_queue)) != 0)
                goto finish;

        struct updated_pkg updated_pkg;

        while (bds_queue_pop(updated_pkg_queue, &updated_pkg)) {
                switch (updated_pkg.status) {
                case PKG_UPDATED:
                        mesg_ok_label("%4s", " %-24s %-8s --> %s\n", "[U]", updated_pkg.name,
                                      updated_pkg.slack_pkg_version, updated_pkg.sbo_version);
                        /*                        printf(COLOR_OK "%4s" COLOR_END " %-24s %-8s --> %s\n", "[U]",
                           updated_pkg.name,
                                                  updated_pkg.slack_pkg_version, updated_pkg.sbo_version); */
                        break;
                case PKG_DOWNGRADED:
                        mesg_info_label("%4s", " %-24s %-8s --> %s\n", "[D]", updated_pkg.name,
                                        updated_pkg.slack_pkg_version, updated_pkg.sbo_version);
                        break;
                default: /* PKG_REMOVED */
                        mesg_error_label("%4s", " %-24s %-8s\n", "[R]", updated_pkg.name,
                                         updated_pkg.slack_pkg_version);
                }
                destroy_updated_pkg(&updated_pkg);
        }

finish:
        if (updated_pkg_queue)
                bds_queue_free(&updated_pkg_queue);

        return rc;
}

static int update_pkgdb(struct pkg_graph *pkg_graph)
{
        int rc                    = 0;
        pkg_nodes_t *sbo_pkgs     = pkg_graph_sbo_pkgs(pkg_graph);
        pkg_nodes_t *new_sbo_pkgs = pkg_nodes_alloc();

        if ((rc = pkg_load_sbo(new_sbo_pkgs)) != 0)
                return rc;

        rc       = pkg_compar_sets(new_sbo_pkgs, sbo_pkgs);
        sbo_pkgs = pkg_graph_assign_sbo_pkgs(pkg_graph, new_sbo_pkgs);

        if (rc != 0)
                return rc;

        if ((rc = pkg_write_db(sbo_pkgs)) != 0)
                return rc;

        return pkg_create_default_deps(sbo_pkgs);
}

static int review_pkg(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        int rc                     = 0;
        pkg_nodes_t *reviewed_pkgs = NULL;
        bool reviewed_pkgs_dirty   = false;

        struct pkg_node *reviewed_node  = NULL;
        const struct pkg_node *pkg_node = NULL;

        struct pkg_options pkg_options = pkg_options_default();

        pkg_options.recursive = false;

        rc = pkg_load_dep(pkg_graph, pkg_name, pkg_options);
        if (rc != 0)
                return rc;

        pkg_node = (const struct pkg_node *)pkg_graph_search(pkg_graph, pkg_name);
        if (pkg_node == NULL) {
                mesg_error("package %s does not exist\n", pkg_name);
                rc = 1;
                return rc;
        }

        reviewed_pkgs = pkg_nodes_alloc();
        if (pkg_reviewed_exists()) {
                rc = pkg_load_reviewed(reviewed_pkgs);
                if (rc != 0)
                        goto finish;
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
                                        reviewed_pkgs_dirty         = true;
                                }
                        } else {
                                reviewed_node = pkg_node_alloc(pkg_node->pkg.name);
                                pkg_init_version(&reviewed_node->pkg, pkg_node->pkg.version);
                                reviewed_node->pkg.info_crc = pkg_node->pkg.info_crc;
                                pkg_nodes_insert_sort(reviewed_pkgs, reviewed_node);
                                reviewed_pkgs_dirty = true;
                        }
                        if (reviewed_pkgs_dirty)
                                rc = pkg_write_reviewed(reviewed_pkgs);
                }
        }
finish:
        if (reviewed_pkgs)
                pkg_nodes_free(&reviewed_pkgs);

        return rc;
}

static int show_pkg_info(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        const struct pkg_node *pkg_node = NULL;

        pkg_node = (const struct pkg_node *)pkg_graph_search(pkg_graph, pkg_name);
        if (pkg_node == NULL) {
                mesg_error("package %s does not exist\n", pkg_name);
                return 1;
        }
        return pkg_show_info(&pkg_node->pkg);
}

static int edit_pkg_dep(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        const struct pkg_node *pkg_node = NULL;

        pkg_node = (const struct pkg_node *)pkg_graph_search(pkg_graph, pkg_name);
        if (pkg_node == NULL) {
                mesg_error("package %s does not exist\n", pkg_name);
                return 1;
        }
        return edit_dep_file(pkg_node->pkg.name);
}

static int write_pkg_sqf(struct pkg_graph *pkg_graph, string_list_t *pkg_names, struct pkg_options pkg_options)
{
        int rc = 0;
        char sqf_file[256];
        struct ostream *os = NULL;

        pkg_nodes_t *reviewed_pkgs = NULL;
        bool reviewed_pkgs_dirty   = false;

        const size_t num_pkgs = string_list_size(pkg_names);

        for (size_t i = 0; i < num_pkgs; ++i) {
                rc = pkg_load_dep(pkg_graph, string_list_get_const(pkg_names, i), pkg_options);
                if (rc != 0)
                        return rc;
        }

        if (pkg_options.revdeps) {
                rc = pkg_load_all_deps(pkg_graph, pkg_options);
                if (rc != 0)
                        return rc;
        }

	if (pkg_options.output_mode == PKG_OUTPUT_FILE &&
	    num_pkgs > 1) 
		assert(pkg_options.output_name);
	
	if( pkg_options.output_name ) {
		bds_string_copyf(sqf_file, sizeof(sqf_file), "%s", pkg_options.output_name);
        } else {
                if (pkg_options.revdeps) {
                        bds_string_copyf(sqf_file, sizeof(sqf_file), "%s-revdeps.sqf",
                                         string_list_get_const(pkg_names, 0));
                } else {
                        bds_string_copyf(sqf_file, sizeof(sqf_file), "%s.sqf",
                                         string_list_get_const(pkg_names, 0));
                }
        }

        reviewed_pkgs = pkg_nodes_alloc();
        if (pkg_reviewed_exists()) {
                rc = pkg_load_reviewed(reviewed_pkgs);
                if (rc != 0)
                        goto finish;
        }

	bool buffer_stream = (pkg_options.output_mode != PKG_OUTPUT_FILE);
	const char *output_path =
		(pkg_options.output_mode == PKG_OUTPUT_FILE ? &sqf_file[0] : "/dev/stdout");
	os = ostream_open(output_path, "w", buffer_stream);

	if (os == NULL) {
		mesg_error("unable to create %s\n", output_path);
		rc = 1;
		goto finish;
	}
	rc = write_sqf(os, pkg_graph, pkg_names, pkg_options, reviewed_pkgs, &reviewed_pkgs_dirty);

	if (rc < 0) {
		goto finish;
	}

        if (reviewed_pkgs_dirty) {
                rc = pkg_write_reviewed(reviewed_pkgs);
        }
finish:
        if (reviewed_pkgs)
                pkg_nodes_free(&reviewed_pkgs);

        if (os)
                ostream_close(os);

        return rc;
}

static int write_pkg_update_sqf(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options pkg_options)
{
        return 0;
}

static int write_pkg_remove_sqf(struct pkg_graph *pkg_graph, const string_list_t *pkg_names, struct pkg_options pkg_options)
{
	return 0;
#if 0
        int rc = 0;
        char sqf_file[256];
        FILE *fp                       = NULL;
        const char *term               = NULL;
        struct bds_queue *pkg_list     = NULL;
        struct bds_stack *removal_list = NULL;
        struct pkg_node *node          = NULL;
        struct pkg_iterator iter;
        struct pkg_options options = pkg_options_default();
        pkg_iterator_flags_t flags = 0;
        int max_dist               = 0;
	const size_t num_pkgs = string_list_size(pkg_names);
	string_list_t *node_pkg_name = string_list_alloc_reference();	

        options.revdeps = true;

        if ((rc = pkg_load_all_deps(pkg_graph, options)) != 0)
                return rc;
	for( size_t i=0; i<num_pkgs; ++i ) {
		if ((rc = pkg_load_dep(pkg_graph, string_list_get_const(pkg_names,i), options)) != 0)
			return rc;
	}

        pkg_list = bds_queue_alloc(1, sizeof(struct pkg_node *), NULL);
        bds_queue_set_autoresize(pkg_list, true);

        removal_list = bds_stack_alloc(1, sizeof(struct pkg_node *), NULL);

        flags    = PKG_ITER_DEPS | PKG_ITER_FORW;
        max_dist = (pkg_options.deep ? -1 : 0);
        for (node = pkg_iterator_begin(&iter, pkg_graph, pkg_names, flags, max_dist); node != NULL;
             node = pkg_iterator_next(&iter)) {

                if (node->pkg.dep.is_meta)
                        continue;

                node->pkg.for_removal = true;
                bds_queue_push(pkg_list, &node);
        }
        pkg_iterator_destroy(&iter);

        while (bds_queue_pop(pkg_list, &node)) {

                flags    = PKG_ITER_REVDEPS;
                max_dist = 1;

		string_list_append(node_pkg_name, node->pkg.name);
		
                for (struct pkg_node *parent_node =
                         pkg_iterator_begin(&iter, pkg_graph, node_pkg_name, flags, max_dist);
                     parent_node != NULL; parent_node = pkg_iterator_next(&iter)) {

                        if (strcmp(parent_node->pkg.name, node->pkg.name) == 0)
                                goto cycle;

                        if (parent_node->pkg.dep.is_meta)
                                goto cycle;

                        if (!parent_node->pkg.for_removal &&
                            slack_pkg_is_installed(parent_node->pkg.name, user_config.sbo_tag)) {
                                mesg_error_label("%12s", " %-24s <-- %s\n", "[required]", node->pkg.name,
                                                 parent_node->pkg.name);
                                node->pkg.for_removal = false;
                                break;
                        }
                }
                pkg_iterator_destroy(&iter);

                if (node->pkg.for_removal)
                        bds_stack_push(removal_list, &node);

	cycle:
		string_list_clear(node_pkg_name);		
        }

        if (bds_stack_size(removal_list) == 0)
                goto finish;

	if( num_pkgs > 1 ) {
		assert(pkg_options.output_name);
                bds_string_copyf(sqf_file, sizeof(sqf_file), "%s", pkg_options.output_name);
	} else {
		bds_string_copyf(sqf_file, sizeof(sqf_file), "%s-remove.sqf", string_list_get_const(pkg_names,0));
	}

        if (pkg_options.output_mode == PKG_OUTPUT_FILE) {
                fp = fopen(sqf_file, "w");
                if (fp == NULL) {
                        mesg_error("unable to create %s\n", sqf_file);
                        rc = 1;
                        goto finish;
                }
                term = "\n";
        } else {
                fp   = stdout;
                term = " ";
        }

        while (bds_stack_pop(removal_list, &node)) {
                fprintf(fp, "%s%s", node->pkg.name, term);
        }

        if (fp != stdout) {
                mesg_ok("created %s\n", sqf_file);
        } else {
                fprintf(fp, "\n");
        }

finish:
        if (fp && fp != stdout)
                fclose(fp);

        if (pkg_list)
                bds_queue_free(&pkg_list);
        if (removal_list)
                bds_stack_free(&removal_list);
	if(node_pkg_name)
		string_list_free(&node_pkg_name);

        return rc;
#endif
}

static int search_pkg(const pkg_nodes_t *sbo_pkgs, const char *pkg_name)
{
        size_t num_nodes            = 0;
        string_list_t *results      = NULL;
        size_t num_results          = 0;
        char *__pkg_name            = NULL;
        const size_t sbo_dir_offset = strlen(user_config.sbopkg_repo) + 1;

        __pkg_name = bds_string_dup(pkg_name);
        bds_string_tolower(__pkg_name);

        results   = string_list_alloc();
        num_nodes = pkg_nodes_size(sbo_pkgs);
        for (size_t i = 0; i < num_nodes; ++i) {
                const struct pkg_node *node = pkg_nodes_get_const(sbo_pkgs, i);
                char *sbo_dir               = bds_string_dup(node->pkg.sbo_dir + sbo_dir_offset);
                char *p                     = bds_string_dup(node->pkg.name);
                if (bds_string_contains(bds_string_tolower(p), __pkg_name)) {
                        string_list_insert_sort(results, sbo_dir);
                } else {
                        free(sbo_dir);
                }
                free(p);
        }
        free(__pkg_name);

        num_results = string_list_size(results);
        for (size_t i = 0; i < num_results; ++i) {
                printf("%s\n", string_list_get_const(results, i));
        }

        if (results)
                string_list_free(&results);

        return 0;
}

static int make_meta_pkg(const pkg_nodes_t *sbo_pkgs, const char *meta_pkg_name, string_list_t *pkg_names)
{
        char meta_pkg_path[4096];
        const size_t num_pkgs = string_list_size(pkg_names);

        if (0 == num_pkgs) {
                mesg_warn("no packages provided for meta package %s\n", meta_pkg_name);
                return 1;
        }

        // Check if the meta package conflicts with an existing package
        if (pkg_nodes_bsearch_const(sbo_pkgs, meta_pkg_name)) {
                mesg_error("meta-package %s conflict with existing %s package\n", meta_pkg_name, meta_pkg_name);
                return 1;
        }

        bds_string_copyf(meta_pkg_path, sizeof(meta_pkg_path), "%s/%s", user_config.depdir, meta_pkg_name);

        FILE *fp = fopen(meta_pkg_path, "w");
        assert(fp);

        fprintf(fp, "METAPKG\n"
                    "REQUIRED:\n");
        for (size_t i = 0; i < num_pkgs; ++i) {
                fprintf(fp, "%s\n", string_list_get_const(pkg_names, i));
        }
        fclose(fp);

        return 0;
}
