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

#include <libbds/bds_string.h>

#include "config.h"
#include "deps.h"
#include "filesystem.h"
#include "pkg_db.h"
#include "user_config.h"

#define LONG_OPT(long_opt, opt)                                                                                   \
        {                                                                                                         \
                long_opt, no_argument, 0, opt                                                                     \
        }

static const char *options_str            = "CDNPRacehlopr";
static const struct option long_options[] = {
    /* These options set a flag. */
    LONG_OPT("check-installed", 'C'),
    LONG_OPT("database", 'D'),
    LONG_OPT("no-recursive", 'N'),
    LONG_OPT("parents", 'P'),
    LONG_OPT("review", 'R'),
    LONG_OPT("add", 'a'),
    LONG_OPT("check-foreign-installed", 'c'),
    LONG_OPT("edit", 'e'),
    LONG_OPT("help", 'h'),
    LONG_OPT("list", 'l'),
    LONG_OPT("optional", 'o'),
    LONG_OPT("print", 'p'),
    LONG_OPT("remove", 'r'),
    {0, 0, 0, 0}};

static const struct option *find_option(const char *long_name, const int name)
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

enum action {
        ACTION_NONE,
        ACTION_REVIEW,
        ACTION_ADD,
        ACTION_CREATEDB,
        ACTION_EDIT,
        ACTION_LIST,
        ACTION_PRINT_TREE,
        ACTION_REMOVE
};

struct action_struct {
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

int main(int argc, char **argv)
{
        bool process_parents   = false;
        bool recursive         = true;
        bool optional          = false;
        bool pkg_name_required = true;
        if (setvbuf(stdout, NULL, _IONBF, 0) != 0)
                perror("setvbuf()");

        init_user_config();

        struct action_struct as = {ACTION_NONE, NULL};

        while (1) {
                int option_index = 0;
                char c           = getopt_long(argc, argv, options_str, long_options, &option_index);

                if (c == -1)
                        break;

                switch (c) {
                case 'C':
                        user_config.check_installed |= CHECK_INSTALLED;
                        break;
                case 'D':
                        set_action(&as, ACTION_CREATEDB, find_option(NULL, 'D'));
                        pkg_name_required = false;
                        break;
                case 'N':
                        recursive = false;
                        break;
                case 'P':
                        process_parents = true;
                        break;
                case 'R':
                        set_action(&as, ACTION_REVIEW, find_option(NULL, 'R'));
                        break;
                case 'a':
                        set_action(&as, ACTION_ADD, find_option(NULL, 'a'));
                        break;
                case 'c':
                        user_config.check_installed |= CHECK_FOREIGN_INSTALLED;
                        break;
                case 'e':
                        set_action(&as, ACTION_EDIT, find_option(NULL, 'e'));
                        break;
                case 'h':
                        print_help();
                        exit(0);
                case 'l':
                        set_action(&as, ACTION_LIST, find_option(NULL, 'l'));
                        break;
                case 'o':
                        optional = true;
                        break;
                case 'p':
                        set_action(&as, ACTION_PRINT_TREE, find_option(NULL, 'p'));
                        break;
                case 'r':
                        set_action(&as, ACTION_REMOVE, find_option(NULL, 'r'));
                        break;
                default:
                        abort();
                }
        }

        const char *pkg_name = NULL;
        if (pkg_name_required) {
                if (argc - optind != 1) {
                        print_help();
                        exit(EXIT_FAILURE);
                }
                pkg_name = argv[optind];
        }

        init_pkg_db();

        int rc = 0;

        /* set_action(&action, ACTION_REVIEW, find_option(NULL, 'R')); */
        /* set_action(&action, ACTION_ADD, find_option(NULL, 'a')); */
        /* set_action(&action, ACTION_CREATEDB, find_option(NULL, 'd')); */
        /* set_action(&action, ACTION_EDIT, find_option(NULL, 'e')); */
        /* set_action(&action, ACTION_LIST, find_option(NULL, 'l')); */
        /* set_action(&action, ACTION_PRINT_TREE, find_option(NULL, 'p')); */
        /* set_action(&action, ACTION_REMOVE, find_option(NULL, 'r')); */

        switch (as.action) {
        case ACTION_REVIEW:
                if (find_dep_file(pkg_name) == NULL) {
                        rc = request_add_dep_file(pkg_name, DEP_REVIEW);
                } else {
                        rc = review_pkg(pkg_name);
		}
                break;
        case ACTION_ADD:
                rc = add_pkg(pkg_db_pkglist, PKGLIST, pkg_name);
                break;
        case ACTION_CREATEDB:
                if ((rc = write_depdb(recursive, optional)) != 0)
                        break;
                rc = write_parentdb(recursive, optional);
		break;
        case ACTION_EDIT:
                rc = edit_dep_file(pkg_name);
                break;
        case ACTION_REMOVE:
                rc = remove_dep_file(pkg_name);
                break;
        }

        destroy_user_config();
        fini_pkg_db();

        return 0;
}

#define PROGRAM_NAME "sbopkg-dep2sqf"
static void print_help()
{
        printf("Usage: %s [options] pkg\n"
               "Options:\n",
               PROGRAM_NAME);
}
