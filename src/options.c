#include <assert.h>
#include <stdio.h>

#include "mesg.h"
#include "options.h"
#include "slack_pkg_dbi.h"

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
        int  prio;
        char optval;
};

#define REVIEW_OPTION_PRIO(__prio_val, __prio_optval)                                                             \
        ({                                                                                                        \
                struct review_option_prio __prio;                                                                 \
                __prio.prio   = __prio_val;                                                                       \
                __prio.optval = __prio_optval;                                                                    \
                __prio;                                                                                           \
        })

static void set_pkg_review_type(enum pkg_review_type *review_type,
                                enum pkg_review_type  type_val,
                                const struct option * long_options)
{
        static struct review_option_prio prio[PKG_REVIEW_SIZE];
        static bool                      initd = false;

        const struct option *prev_option = NULL;
        const struct option *option      = NULL;

        if (!initd) {
                prio[PKG_REVIEW_ENABLED]      = REVIEW_OPTION_PRIO(0, 0);
                prio[PKG_REVIEW_AUTO]         = REVIEW_OPTION_PRIO(1, 'a');
                prio[PKG_REVIEW_AUTO_VERBOSE] = REVIEW_OPTION_PRIO(2, 'A');
                prio[PKG_REVIEW_DISABLED]     = REVIEW_OPTION_PRIO(3, 'i');

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

int process_options(int                  argc,
                    char **              argv,
                    const char *         options_str,
                    const struct option *long_options,
                    void (*__print_help)(void),
                    struct pkg_options *pkg_options)
{
        while (1) {
                int  option_index = 0;
                char c            = getopt_long(argc, argv, options_str, long_options, &option_index);

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
                        pkg_options->revdeps           = true;
                        break;
                case 'p':
                        pkg_options->revdeps = true;
                        break;
                case 'R':
                        pkg_options->pkg_dbi_type = SLACK_PKG_DBI_REPO;
                        break;
                case 'r':
                        pkg_options->rebuild_deps = true;
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
