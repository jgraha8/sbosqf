#ifndef OPTIONS_H__
#define OPTIONS_H__

#include <getopt.h>

#include "pkg.h"

#define LONG_OPT(long_opt, opt)                                                                                   \
        {                                                                                                         \
                long_opt, no_argument, 0, opt                                                                     \
        }

int process_options(int                  argc,
                    char **              argv,
                    const char *         options_str,
                    const struct option *long_options,
                    void (*__print_help)(void),
                    struct pkg_options *pkg_options);

#endif // OPTIONS_H__
