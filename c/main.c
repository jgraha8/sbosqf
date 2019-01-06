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
#include "input.h"
#include "msg_string.h"
#include "pkg_db.h"
#include "user_config.h"

#define LONG_OPT(long_opt, opt)                                                                                   \
        {                                                                                                         \
                long_opt, no_argument, 0, opt                                                                     \
        }

static const char *options_str            = "CDNPRacehlmpr";
static const struct option long_options[] = {
    /* These options set a flag. */
    LONG_OPT("check-installed", 'C'),
    LONG_OPT("database", 'D'),
    LONG_OPT("no-recursive", 'N'),
    LONG_OPT("revdeps", 'P'),
    LONG_OPT("review", 'R'),
    LONG_OPT("add", 'a'),
    LONG_OPT("check-foreign-installed", 'c'),
    LONG_OPT("edit", 'e'),
    LONG_OPT("help", 'h'),
    LONG_OPT("list", 'l'),
    LONG_OPT("menu", 'm'),
/*    LONG_OPT("optional", 'o'), */
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
        ACTION_MENU,
        ACTION_LIST,
        ACTION_PRINT_TREE,
        ACTION_REMOVE,
        ACTION_EDIT,
        ACTION_MANAGE_DEP,
	ACTION_WRITE_SQF
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
        struct process_options process_options;
        bool pkg_name_required = true;

        if (setvbuf(stdout, NULL, _IONBF, 0) != 0)
                perror("setvbuf()");

	memset(&process_options, 0, sizeof(process_options));
	process_options.recursive = true;
	process_options.optional = true;	

        init_user_config();

        struct action_struct as = {ACTION_NONE, NULL};

        while (1) {
                int option_index = 0;
                char c           = getopt_long(argc, argv, options_str, long_options, &option_index);

                if (c == -1)
                        break;

                switch (c) {
                case 'C':
                        process_options.check_installed |= CHECK_INSTALLED;
                        break;
                case 'D':
                        set_action(&as, ACTION_CREATEDB, find_option(NULL, 'D'));
                        pkg_name_required = false;
                        break;
                case 'N':
                        process_options.recursive = false;
                        break;
                case 'P':
                        process_options.revdeps = true;
                        break;
                case 'R':
                        set_action(&as, ACTION_REVIEW, find_option(NULL, 'R'));
                        break;
                case 'm':
                        set_action(&as, ACTION_MENU, find_option(NULL, 'm'));
                        break;
                case 'a':
                        set_action(&as, ACTION_ADD, find_option(NULL, 'a'));
                        break;
                case 'c':
                        process_options.check_installed |= CHECK_ANY_INSTALLED;
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
                /* case 'o': */
                /*         process_options.optional = true; */
                /*         break; */
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

	if( as.action == 0 ) {
		as.action = ACTION_WRITE_SQF;
	}

        switch (as.action) {
        case ACTION_REVIEW:
                if (find_dep_file(pkg_name) == NULL) {
                        rc = display_dep_menu(pkg_name, msg_dep_file_not_found(pkg_name), 0);
                } else {
                        rc = perform_dep_action(pkg_name, MENU_REVIEW_PKG);
                }
                goto finish;
        case ACTION_ADD:
                rc = perform_dep_action(pkg_name, MENU_ADD_PKG);
                goto finish;
        case ACTION_CREATEDB:
                if ((rc = write_depdb(process_options)) != 0)
                        break;
                rc = write_parentdb(process_options);
                goto finish;
        case ACTION_EDIT:
                rc = perform_dep_action(pkg_name, MENU_EDIT_DEP);
                goto finish;
        case ACTION_REMOVE:
                rc = perform_dep_action(pkg_name, MENU_REMOVE_DEP);
                goto finish;
        case ACTION_MENU:
                if (find_dep_file(pkg_name) == NULL) {
                        rc = display_dep_menu(pkg_name, msg_dep_file_not_found(pkg_name), 0);
                } else {
			rc = display_dep_menu(pkg_name, NULL, 0);
		}
                goto finish;
        case ACTION_LIST:
	case ACTION_WRITE_SQF: {
                if (find_dep_file(pkg_name) == NULL) {
                        rc = display_dep_menu(pkg_name, msg_dep_file_not_found(pkg_name), 0);
                }
		struct dep_list *dep_list = NULL;		
		if( process_options.revdeps ) {
			dep_list = (struct dep_list *)load_dep_parents(pkg_name, process_options);
		} else {
			dep_list = load_dep_list(pkg_name, process_options);
		}
		
                if (dep_list == NULL) {
                        fprintf(stderr, "unable to load dependency list for %s\n", pkg_name);
                        rc = 1;
			goto finish;
                }

		if( as.action == ACTION_LIST ) {
			write_dep_list(stdout, dep_list);
			rc = 0;
			goto finish;
                }

		char sqf[256];
		if( process_options.revdeps ) {
			bds_string_copyf(sqf, sizeof(sqf), "%s-revdeps.sqf", pkg_name);
		} else {
			bds_string_copyf(sqf, sizeof(sqf), "%s.sqf", pkg_name);
		}
		FILE *fp = fopen(sqf, "w");
		if( fp == NULL ) {
			fprintf(stderr, "unable to write sqf file %s\n", sqf);
			rc = 1;
			goto finish;
		}	
		write_sqf(fp, dep_list, process_options);
		fclose(fp);
		
		rc = 0;
                goto finish;		
	}
	default:
		printf("action %d not handled\n", as.action);
        }

finish:
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
