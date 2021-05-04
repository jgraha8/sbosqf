#ifndef IO_H__
#define IO_H__

#include "string_list.h"
#include "pkg.h"
#include "pkg_graph.h"
#include "slack_pkg_dbi.h"
#include "ostream.h"


int io_write_sqf(struct ostream *os, const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph,
		 const string_list_t *pkg_names, struct pkg_options options, bool *db_dirty);

int io_load_dep(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options options);

#endif // IO_H__
