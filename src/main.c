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
#include "slack_pkg_dbi.h"
#include "string_list.h"
#include "user_config.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define LONG_OPT(long_opt, opt)                                                                                   \
        {                                                                                                         \
                long_opt, no_argument, 0, opt                                                                     \
        }

static bool pkg_name_required               = true;
static bool pkg_name_optional               = false;
static bool multiple_pkg_names              = false;
static bool create_graph                    = true;
static bool dep_file_required               = true;
static enum slack_pkg_dbi_type pkg_dbi_type = SLACK_PKG_DBI_PACKAGES;

enum action {
        ACTION_NONE,
        ACTION_CHECK_UPDATES,
        ACTION_UPDATEDB,
        ACTION_REVIEW,
        ACTION_SHOW_INFO,
        ACTION_SEARCH_PKG,
        ACTION_EDIT_DEP,
        ACTION_HELP,
        ACTION_CREATE_REMOVE_SQF,
        ACTION_CREATE_SQF,
        ACTION_CREATE_UPDATE_SQF,
        ACTION_MAKE_META,
        ACTION_TRACK,
        ACTION_UNTRACK,
        ACTION_SHOW_TRACK,
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

static int check_updates(const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph,
                         const char *pkg_name);

static int update_pkgdb(struct pkg_graph *pkg_graph);

static int edit_pkg_dep(struct pkg_graph *pkg_graph, const char *pkg_name);

static int write_pkg_sqf(const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph,
                         string_list_t *pkg_names, struct pkg_options pkg_options);

static int write_pkg_update_sqf(const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph,
                                const string_list_t *pkg_names, struct pkg_options pkg_options);

static int write_pkg_remove_sqf(const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph,
                                const string_list_t *pkg_names, struct pkg_options pkg_options);

static int review_pkg(struct pkg_graph *pkg_graph, const char *pkg_name);

static int show_pkg_info(struct pkg_graph *pkg_graph, const char *pkg_name);

static int search_pkg(struct pkg_graph *pkg_graph, const char *pkg_name);

static int make_meta_pkg(const pkg_nodes_t *sbo_pkgs, const char *meta_pkg_name, string_list_t *pkg_names);

static int manage_track_pkg(pkg_nodes_t *sbo_pkgs, string_list_t *pkg_names, bool track);

static int show_track_pkg(const pkg_nodes_t *sbo_pkgs, const string_list_t *pkg_names, struct pkg_options options);

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
                case 'C':
                        pkg_options->check_installed |= PKG_CHECK_ANY_INSTALLED;
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
                case 'l':
                        pkg_options->output_mode = PKG_OUTPUT_STDOUT;
                        break;
                case 'L': {
                        long int val = strtol(optarg, NULL, 10);
                        switch (val) {
                        case 1:
                                pkg_options->output_mode = PKG_OUTPUT_SLACKPKG_1;
                                break;
                        case 2:
                                pkg_options->output_mode = PKG_OUTPUT_SLACKPKG_2;
                                break;
                        default:
                                mesg_error("option --list-slackpkg/-L requires a value of 1 or 2\n");
                                exit(EXIT_FAILURE);
                        }
                } break;
                case 'n':
                        pkg_options->recursive = false;
                        break;
                case 'o':
                        pkg_options->output_name = optarg;
                        break;
                case 'P':
                        pkg_options->installed_revdeps = true;
                        pkg_options->revdeps = true;
                        break;
                case 'p':
                        pkg_options->revdeps = true;
                        break;
                case 'R':
                        pkg_dbi_type = SLACK_PKG_DBI_REPO;
                        break;
                case 'r':
                        pkg_options->rebuild_deps = true;
                        break;
                case 't':
                        pkg_options->track_mode = PKG_TRACK_ENABLE;
                        break;
                case 'T':
                        pkg_options->track_mode = PKG_TRACK_ENABLE_ALL;
                        break;
                case 'z':
                        pkg_options->all_packages = true;
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
               "  -L, --list-slackpkg {1|2}\n"
               "  -o, --output\n"
               "  -n, --no-recursive\n"
               "  -p, --revdeps\n"
               "  -P, --installed-revdeps\n"
               "  -R, --repo-db\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

static int cmd_create_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *options_str            = "aAibcCdhlL:o:nPpRtT";
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
                                                     LONG_OPT("no-recursive", 'n'), /* option */
                                                     LONG_OPT("installed-revdeps", 'P'),      /* option */
                                                     LONG_OPT("revdeps", 'p'),      /* option */
                                                     LONG_OPT("repo-db", 'R'),
                                                     {0, 0, 0, 0}};

        int rc = process_options(argc, argv, options_str, long_options, cmd_create_print_help, options);

        if (rc >= 0) {
                if (options->output_mode != PKG_OUTPUT_FILE && options->output_name) {
                        mesg_error(
                            "options --list/-l, --list-slackpkg/-L, and --output/-o are mutually exclusive\n");
                        return -1;
                }
        }

        return rc;
}

static void cmd_update_print_help()
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

static int cmd_update_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *options_str            = "AaihlL:o:PRrz";
        static const struct option long_options[] = {/* These options set a flag. */
                                                     LONG_OPT("auto-review-verbose", 'A'), /* option */
                                                     LONG_OPT("auto-review", 'a'),         /* option */
                                                     LONG_OPT("ignore-review", 'i'),       /* option */
                                                     LONG_OPT("help", 'h'),
                                                     LONG_OPT("list", 'l'),
                                                     LONG_OPT("list-slackpkg", 'L'),
                                                     LONG_OPT("output", 'o'),
                                                     LONG_OPT("installed-revdeps", 'P'),      /* option */
                                                     LONG_OPT("repo-db", 'R'),
                                                     LONG_OPT("rebuild-deps", 'r'),
                                                     {0, 0, 0, 0}};

        int rc = process_options(argc, argv, options_str, long_options, cmd_update_print_help, options);

        if (rc >= 0) {
                if (options->output_mode != PKG_OUTPUT_FILE && options->output_name) {
                        mesg_error(
                            "options --list/-l, --list-slackpkg/-L, and --output/-o are mutually exclusive\n");
                        return -1;
                }
        }

        return rc;
}

static void cmd_remove_print_help()
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

static int cmd_remove_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *options_str            = "dhlL:o:R";
        static const struct option long_options[] = {                       /* These options set a flag. */
                                                     LONG_OPT("deep", 'd'), /* option */
                                                     LONG_OPT("help", 'h'),          LONG_OPT("list", 'l'),
                                                     LONG_OPT("list-slackpkg", 'L'), LONG_OPT("output", 'o'),
                                                     LONG_OPT("repo-db", 'R'),       {0, 0, 0, 0}};

        int rc = process_options(argc, argv, options_str, long_options, cmd_remove_print_help, options);

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
               "  -h, --help\n"
               "  -R, --repo-db\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

static int cmd_check_updates_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *options_str            = "hR";
        static const struct option long_options[] = {
            LONG_OPT("help", 'h'), LONG_OPT("repo-db", 'R'), {0, 0, 0, 0}};

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

static void cmd_track_print_help()
{
        printf("Usage: %s track [option] [pkg]\n"
               "Marks packages as tracked\n"
               "\n"
               "Options:\n"
               "  -h, --help\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

static int cmd_track_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *options_str            = "h";
        static const struct option long_options[] = {LONG_OPT("help", 'h'), {0, 0, 0, 0}};

        return process_options(argc, argv, options_str, long_options, cmd_track_print_help, options);
}

static void cmd_untrack_print_help()
{
        printf("Usage: %s untrack [option] [pkg]\n"
               "Unmarks packages as tracked\n"
               "\n"
               "Options:\n"
               "  -h, --help\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

static int cmd_untrack_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *options_str            = "h";
        static const struct option long_options[] = {LONG_OPT("help", 'h'), {0, 0, 0, 0}};

        return process_options(argc, argv, options_str, long_options, cmd_untrack_print_help, options);
}

static void cmd_show_track_print_help()
{
        printf("Usage: %s show-track [option] [pkg]\n"
               "Checks if packages are tracked\n"
               "\n"
               "Options:\n"
               "  -h, --help\n"
               " --all\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

static int cmd_show_track_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *options_str            = "h";
        static const struct option long_options[] = {LONG_OPT("help", 'h'), LONG_OPT("all", 'z'), {0, 0, 0, 0}};

        return process_options(argc, argv, options_str, long_options, cmd_show_track_print_help, options);
}

int main(int argc, char **argv)
{
        int rc = 0;

        struct pkg_graph *pkg_graph    = NULL;
        pkg_nodes_t *sbo_pkgs          = NULL;
        struct pkg_options pkg_options = pkg_options_default();
        string_list_t *pkg_names       = NULL;
        struct slack_pkg_dbi slack_pkg_dbi;

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

                const char *cmd = argv[1];
                if (strcmp(cmd, "create") == 0) {

                        as.action          = ACTION_CREATE_SQF;
                        num_opts           = cmd_create_options(argc, argv, &pkg_options);
                        multiple_pkg_names = true;

                } else if (strcmp(cmd, "remove") == 0) {

                        as.action          = ACTION_CREATE_REMOVE_SQF;
                        num_opts           = cmd_remove_options(argc, argv, &pkg_options);
                        multiple_pkg_names = true;

                } else if (strcmp(cmd, "update") == 0) {

                        as.action          = ACTION_CREATE_UPDATE_SQF;
                        num_opts           = cmd_update_options(argc, argv, &pkg_options);
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

                } else if (strcmp(cmd, "track") == 0) {

                        as.action          = ACTION_TRACK;
                        num_opts           = cmd_track_options(argc, argv, &pkg_options);
                        multiple_pkg_names = true;
                        dep_file_required  = false;

                } else if (strcmp(cmd, "untrack") == 0) {

                        as.action          = ACTION_UNTRACK;
                        num_opts           = cmd_untrack_options(argc, argv, &pkg_options);
                        multiple_pkg_names = true;
                        dep_file_required  = false;

                } else if (strcmp(cmd, "show-track") == 0) {

                        as.action          = ACTION_SHOW_TRACK;
                        num_opts           = cmd_show_track_options(argc, argv, &pkg_options);
                        multiple_pkg_names = true;
                        dep_file_required  = false;
                        pkg_name_required  = !pkg_options.all_packages;

                } else {

                        mesg_error("incorrect command provided: %s\n", cmd);
                        exit(EXIT_FAILURE);
                }

                if (num_opts < 0) {
                        mesg_error("unable to process command %s\n", cmd);
                        exit(EXIT_FAILURE);
                }

                argc -= MAX(num_opts + 1, 1);
                argv += MAX(num_opts + 1, 1);
        }

        slack_pkg_dbi = slack_pkg_dbi_create(pkg_dbi_type);
        pkg_graph     = pkg_graph_alloc();
        sbo_pkgs      = pkg_graph_sbo_pkgs(pkg_graph);

        if (create_graph) {
                if (!pkg_db_exists()) {
                        pkg_load_sbo(sbo_pkgs);
                        if ((rc = pkg_write_db(sbo_pkgs)) != 0) {
                                return 1;
                        }
                        pkg_create_default_deps(sbo_pkgs);
                } else {
                        pkg_load_db(sbo_pkgs);
                }
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
                                if (dep_file_required) {
                                        if (!dep_file_exists(argv[i])) {
                                                mesg_error("dependency file %s does not exist\n", argv[i]);
                                                exit(EXIT_FAILURE);
                                        }
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

        if (as.action == 0) {
                as.action = ACTION_CREATE_SQF;
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
                rc = check_updates(&slack_pkg_dbi, pkg_graph, pkg_name);
                if (rc != 0) {
                        if (pkg_name) {
                                mesg_error("unable to check updates for package %s\n", pkg_name);
                        } else {
                                mesg_error("unable to check updates\n");
                        }
                }
                break;
        case ACTION_CREATE_UPDATE_SQF:
                rc = write_pkg_update_sqf(&slack_pkg_dbi, pkg_graph, pkg_names, pkg_options);
                if (rc != 0) {
                        mesg_error("unable to create update package list\n");
                }
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
        case ACTION_CREATE_SQF:
                rc = write_pkg_sqf(&slack_pkg_dbi, pkg_graph, pkg_names, pkg_options);
                if (rc != 0) {
                        mesg_error("unable to create dependency list for package %s\n", pkg_name);
                }
                break;
        case ACTION_CREATE_REMOVE_SQF:
                rc = write_pkg_remove_sqf(&slack_pkg_dbi, pkg_graph, pkg_names, pkg_options);
                if (rc != 0) {
                        mesg_error("unable to create remove package list\n");
                }
                break;
        case ACTION_SEARCH_PKG:
                rc = search_pkg(pkg_graph, pkg_name);
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
        case ACTION_TRACK:
        case ACTION_UNTRACK:
                rc = manage_track_pkg(sbo_pkgs, pkg_names, as.action == ACTION_TRACK);
                if (rc != 0) {
                        mesg_error("unable to update tracking\n");
                } else {
                        mesg_ok("updated package tracking\n");
                }
                break;
        case ACTION_SHOW_TRACK:
                rc = show_track_pkg(sbo_pkgs, pkg_names, pkg_options);
                if (rc != 0) {
                        mesg_error("unable to check tracking\n");
                        /*                } else {
                                                mesg_ok("updated package tracking\n");
                        */
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
        PKG_CHANGE_NONE = 0,
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

struct bds_vector *create_pkg_status_vector()
{
        return bds_vector_alloc(1, sizeof(struct updated_pkg), (void (*)(void *))destroy_updated_pkg);
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static int get_updated_pkgs(const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph,
                            const char *pkg_name, struct bds_queue *updated_pkg_queue)
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
        const ssize_t num_slack_pkgs = slack_pkg_dbi->size();
        if (num_slack_pkgs < 0)
                return 1;

        for (ssize_t i = 0; i < num_slack_pkgs; ++i) {

                struct updated_pkg updated_pkg;

                const struct slack_pkg *slack_pkg = slack_pkg_dbi->get_const((size_t)i, user_config.sbo_tag);
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
                        /* Check if it's installed/present */
                        const struct slack_pkg *slack_pkg =
                            slack_pkg_dbi->search_const(pkg_name, user_config.sbo_tag);
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

static int check_updates(const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph,
                         const char *pkg_name)
{
        int rc = 0;

        struct bds_queue *updated_pkg_queue =
            bds_queue_alloc(1, sizeof(struct updated_pkg), (void (*)(void *))destroy_updated_pkg);

        bds_queue_set_autoresize(updated_pkg_queue, true);

        if ((rc = get_updated_pkgs(slack_pkg_dbi, pkg_graph, pkg_name, updated_pkg_queue)) != 0)
                goto finish;

        struct updated_pkg updated_pkg;

        while (bds_queue_pop(updated_pkg_queue, &updated_pkg)) {
                switch (updated_pkg.status) {
                case PKG_UPDATED:
                        mesg_ok_label("%4s", " %-24s %-8s --> %s\n", "[U]", updated_pkg.name,
                                      updated_pkg.slack_pkg_version, updated_pkg.sbo_version);
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
        int rc        = 0;
        bool db_dirty = false;

        struct pkg_node *pkg_node      = NULL;
        struct pkg_options pkg_options = pkg_options_default();

        pkg_options.recursive = false;

        rc = pkg_load_dep(pkg_graph, pkg_name, pkg_options);
        if (rc != 0)
                return rc;

        pkg_node = pkg_graph_search(pkg_graph, pkg_name);
        if (pkg_node == NULL) {
                mesg_error("package %s does not exist\n", pkg_name);
                rc = 1;
                return rc;
        }

        if (pkg_node->pkg.is_reviewed) {
                rc = pkg_review(&pkg_node->pkg);
        } else {
                int dep_status;
                rc = pkg_review_prompt(&pkg_node->pkg, 0, &dep_status);
                if (rc == 0) {
                        pkg_node->pkg.is_reviewed = true;
                        db_dirty                  = true;
                }
        }

        if (db_dirty)
                pkg_write_db(pkg_graph_sbo_pkgs(pkg_graph));

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
        int rc                    = 0;
        struct pkg_node *pkg_node = NULL;

        pkg_node = (struct pkg_node *)pkg_graph_search(pkg_graph, pkg_name);
        if (pkg_node == NULL) {
                mesg_error("package %s does not exist\n", pkg_name);
                return 1;
        }

        rc = edit_dep_file(pkg_node->pkg.name);
        if (rc != 0)
                return rc;

        /*
          Mark not reviewed
         */
        pkg_node->pkg.is_reviewed = false;

        return pkg_write_db(pkg_graph_sbo_pkgs(pkg_graph));
}

static int __write_pkg_sqf(const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph,
                           string_list_t *pkg_names, const char *output_path, struct pkg_options pkg_options,
                           bool *db_dirty)
{
        int rc = 0;

        struct ostream *os = ostream_open(output_path, "w", (0 == strcmp(output_path, "/dev/stdout")));

        if (os == NULL) {
                mesg_error("unable to create %s\n", output_path);
                return 1;
        }
        rc = write_sqf(os, slack_pkg_dbi, pkg_graph, pkg_names, pkg_options, db_dirty);

        if (os)
                ostream_close(os);

        return rc;
}

static int __write_pkg_nodes_sqf(const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph,
                                 pkg_nodes_t *pkg_nodes, const char *output_path, struct pkg_options pkg_options,
                                 bool *db_dirty)
{
        int rc = 0;

        string_list_t *pkg_names = string_list_alloc_reference();

        for (size_t i = 0; i < pkg_nodes_size(pkg_nodes); ++i) {
                string_list_append(pkg_names, pkg_nodes_get_const(pkg_nodes, i)->pkg.name);
        }

        rc = __write_pkg_sqf(slack_pkg_dbi, pkg_graph, pkg_names, output_path, pkg_options, db_dirty);

        string_list_free(&pkg_names);

        return rc;
}

static const char *get_output_path(struct pkg_options pkg_options, const string_list_t *pkg_names)
{
        static char buf[256]    = {0};
        const char *output_path = "/dev/stdout";

        if (pkg_options.output_mode == PKG_OUTPUT_FILE) {
                if (string_list_size(pkg_names) > 1)
                        assert(pkg_options.output_name);

                if (pkg_options.output_name) {
                        bds_string_copyf(buf, sizeof(buf), "%s", pkg_options.output_name);
                } else {
                        bds_string_copyf(buf, sizeof(buf), "%s.sqf", string_list_get_const(pkg_names, 0));
                }
                output_path = buf;
        }
        return output_path;
}

static int write_pkg_sqf(const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph,
                         string_list_t *pkg_names, struct pkg_options pkg_options)
{
        int rc = 0;

        struct ostream *os    = NULL;
        bool db_dirty         = false;
        const size_t num_pkgs = string_list_size(pkg_names);

        for (size_t i = 0; i < num_pkgs; ++i) {
                rc = pkg_load_dep(pkg_graph, string_list_get_const(pkg_names, i), pkg_options);
                if (rc != 0)
                        return rc;
        }

	if( pkg_options.revdeps ) {
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
static int select_updated_pkgs(const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph,
                               const string_list_t *pkg_names, pkg_nodes_t *updated_pkgs)
{
        struct pkg_node *node             = NULL;
        const struct slack_pkg *slack_pkg = NULL;
        const char *pkg_name              = NULL;
        const int flags                   = PKG_ITER_DEPS;
        const int max_dist                = 0;
        struct pkg_iterator iter;

        for (size_t i = 0; i < string_list_size(pkg_names); ++i) {
                pkg_name = string_list_get_const(pkg_names, i);

                for (node = pkg_iterator_begin(&iter, pkg_graph, pkg_name, flags, max_dist); node != NULL;
                     node = pkg_iterator_next(&iter)) {

                        if (node->pkg.dep.is_meta)
                                continue;

                        slack_pkg = slack_pkg_dbi->search_const(node->pkg.name, user_config.sbo_tag);
                        if (slack_pkg) {
                                if (compar_versions(node->pkg.version, slack_pkg->version) > 0) {
                                        pkg_nodes_append_unique(updated_pkgs, node);
                                }
                        }
                }
                pkg_iterator_destroy(&iter);
        }

        return 0;
}

static int update_process_deps(const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph,
                               const bool rebuild_deps, pkg_nodes_t *pkg_list, pkg_nodes_t *update_list,
                               pkg_nodes_t *build_list)
{
        int rc = 0;

        assert(pkg_nodes_size(update_list) == 0);

        while (pkg_nodes_size(pkg_list) > 0) {

                struct pkg_node *cur_node = pkg_nodes_get(pkg_list, 0);

                /*
                  Process dependencies
                */
                int flags    = PKG_ITER_DEPS | PKG_ITER_PRESERVE_COLOR;
                int max_dist = -1;
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

                        int ver_diff = compar_versions(node->pkg.version, slack_pkg->version);
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

static int update_process_revdeps(const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph,
                                  pkg_nodes_t *pkg_list, pkg_nodes_t *update_list, pkg_nodes_t *build_list)
{
        int rc = 0;

        assert(pkg_nodes_size(pkg_list) == 0);

        while (pkg_nodes_size(update_list) > 0) {

                struct pkg_node *cur_node = pkg_nodes_get(update_list, 0);

                /*
                  Process parents for updated nodes
                */
                int flags    = PKG_ITER_REVDEPS | PKG_ITER_FORW | PKG_ITER_PRESERVE_COLOR;
                int max_dist = 1;
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

                        int ver_diff = compar_versions(node->pkg.version, slack_pkg->version);
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

static int update_process(const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph,
                          struct pkg_options pkg_options, pkg_nodes_t *pkg_list, pkg_nodes_t *update_list,
                          pkg_nodes_t *build_list)
{
        int rc                        = 0;
        bool db_dirty                 = false;
        pkg_nodes_t *input_pkg_list   = pkg_nodes_alloc_reference();
        pkg_nodes_t *review_skip_list = pkg_nodes_alloc_reference();

        const enum pkg_review_type review_type = pkg_options.review_type;
        const bool rebuild_deps                = pkg_options.rebuild_deps;

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
                        rc = update_process_deps(slack_pkg_dbi, pkg_graph, rebuild_deps, pkg_list, update_list,
                                                 build_list);
                        if (rc != 0) {
                                goto finish;
                        }

                        rc = update_process_revdeps(slack_pkg_dbi, pkg_graph, pkg_list, update_list, build_list);
                        if (rc != 0) {
                                goto finish;
                        }
                }

                for (size_t i = 0; i < pkg_nodes_size(build_list); ++i) {
                        struct pkg_node *node = pkg_nodes_get(build_list, i);

                        if (pkg_nodes_bsearch_const(review_skip_list, node->pkg.name)) {
                                continue;
                        }

                        rc = check_reviewed_pkg(&pkg_nodes_get(build_list, i)->pkg, review_type, &db_dirty);
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

static int write_pkg_update_sqf(const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph,
                                const string_list_t *pkg_names, struct pkg_options pkg_options)
{
        int rc = 0;

        pkg_nodes_t *pkg_list    = NULL;
        pkg_nodes_t *update_list = NULL;
        pkg_nodes_t *build_list  = NULL;
        bool db_dirty            = false;

        pkg_list    = pkg_nodes_alloc_reference();
        update_list = pkg_nodes_alloc_reference();
        build_list  = pkg_nodes_alloc_reference();

        pkg_options.revdeps  = true;
        pkg_options.deep     = true;
        pkg_options.max_dist = -1;

	if(pkg_options.installed_revdeps) {
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
                if (is_meta_pkg(pkg_name)) {
                        assert(pkg_load_dep(pkg_graph, pkg_name, pkg_options) == 0);
                }
        }

        rc = select_updated_pkgs(slack_pkg_dbi, pkg_graph, pkg_names, pkg_list);
        if (rc != 0) {
                goto finish;
        }

        rc = update_process(slack_pkg_dbi, pkg_graph, pkg_options, pkg_list, update_list, build_list);
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

        rc = __write_pkg_nodes_sqf(slack_pkg_dbi, pkg_graph, build_list, get_output_path(pkg_options, pkg_names),
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

static int write_pkg_remove_sqf(const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph,
                                const string_list_t *pkg_names, struct pkg_options pkg_options)
{

        int rc = 0;
        char sqf_file[256];
        struct ostream *os             = NULL;
        pkg_nodes_t *pkg_list          = NULL;
        struct bds_stack *removal_list = NULL;
        struct pkg_node *node          = NULL;
        struct pkg_iterator iter;
        pkg_iterator_flags_t flags = 0;
        int max_dist               = 0;
        const size_t num_pkgs      = string_list_size(pkg_names);

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

        bool buffer_stream      = (pkg_options.output_mode != PKG_OUTPUT_FILE);
        const char *output_path = (pkg_options.output_mode == PKG_OUTPUT_FILE ? &sqf_file[0] : "/dev/stdout");
        os                      = ostream_open(output_path, "w", buffer_stream);

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

static int __search_pkg_nodes(const pkg_nodes_t *pkg_nodes, const char *pkg_name, string_list_t *results)
{
        char *__pkg_name            = NULL;
        const size_t sbo_dir_offset = strlen(user_config.sbopkg_repo) + 1;

        __pkg_name = bds_string_dup(pkg_name);
        bds_string_tolower(__pkg_name);

        for (size_t i = 0; i < pkg_nodes_size(pkg_nodes); ++i) {
                const struct pkg_node *node = pkg_nodes_get_const(pkg_nodes, i);

                char *p = bds_string_dup(node->pkg.name);

                if (bds_string_contains(bds_string_tolower(p), __pkg_name)) {
                        char *sbo_dir = NULL;
                        if (node->pkg.dep.is_meta) {
                                sbo_dir = bds_string_dup_concat(2, "META/", node->pkg.name);
                        } else {
                                sbo_dir = bds_string_dup(node->pkg.sbo_dir + sbo_dir_offset);
                        }

                        string_list_insert_sort(results, sbo_dir);
                }
                free(p);
        }
        free(__pkg_name);

        return 0;
}

static int search_pkg(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        int rc                 = 0;
        string_list_t *results = NULL;

        results = string_list_alloc();
        rc      = __search_pkg_nodes(pkg_graph_sbo_pkgs(pkg_graph), pkg_name, results);
        if (rc != 0)
                goto finish;

        /*
          Load all meta packages into the graph (as nodes)
        */
        find_all_meta_pkgs(pkg_graph_meta_pkgs(pkg_graph));
        rc = __search_pkg_nodes(pkg_graph_meta_pkgs(pkg_graph), pkg_name, results);
        if (rc != 0)
                goto finish;

        for (size_t i = 0; i < string_list_size(results); ++i) {
                printf("%s\n", string_list_get_const(results, i));
        }

finish:
        if (results)
                string_list_free(&results);

        return rc;
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

static int manage_track_pkg(pkg_nodes_t *sbo_pkgs, string_list_t *pkg_names, bool track)
{
        for (size_t i = 0; i < string_list_size(pkg_names); ++i) {
                const char *pkg_name  = string_list_get_const(pkg_names, i);
                struct pkg_node *node = pkg_nodes_bsearch(sbo_pkgs, pkg_name);

                if (NULL == node) {
                        mesg_error("package %s not found\n", pkg_name);
                        return 1;
                }
                node->pkg.is_tracked = track;
        }

        return pkg_write_db(sbo_pkgs);
}

static int show_track_pkg(const pkg_nodes_t *sbo_pkgs, const string_list_t *pkg_names, struct pkg_options options)
{
        string_list_t *__pkg_names = NULL;
        if (options.all_packages) {
                __pkg_names = string_list_alloc_reference();
                for (size_t i = 0; i < pkg_nodes_size(sbo_pkgs); ++i) {
                        const struct pkg_node *node = pkg_nodes_get_const(sbo_pkgs, i);
                        if (node->pkg.is_tracked) {
                                string_list_append(__pkg_names, node->pkg.name);
                        }
                }
                pkg_names = __pkg_names;
        }

        for (size_t i = 0; i < string_list_size(pkg_names); ++i) {
                const char *pkg_name        = string_list_get_const(pkg_names, i);
                const struct pkg_node *node = pkg_nodes_bsearch_const(sbo_pkgs, pkg_name);

                if (NULL == node) {
                        mesg_error("package %s not found\n", pkg_name);
                        return 1;
                }

                if (node->pkg.is_tracked) {
                        mesg_ok_label("[T]", " %s\n", pkg_name);
                } else {
                        mesg_error_label("[U]", " %s\n", pkg_name);
                }
        }

        if (options.all_packages) {
                string_list_free(&__pkg_names);
        }

        return 0;
}
