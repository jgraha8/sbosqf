#ifndef OUTPUT_PATH_H__
#define OUTPUT_PATH_H__

#include "pkg.h"
#include "string_list.h"

const char *get_output_path(struct pkg_options pkg_options, const string_list_t *pkg_names);

#endif // OUTPUT_PATH_H__
