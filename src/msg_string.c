#include <libbds/bds_string.h>

#include "msg_string.h"

const char *msg_dep_file_not_found(const char *pkg_name)
{
	static char msg[4096];

	bds_string_copyf(msg, sizeof(msg), "+ Dependency file %s not found", pkg_name);
	return msg;
}

const char *msg_pkg_not_reviewed(const char *pkg_name)
{
	static char msg[4096];

	bds_string_copyf(msg, sizeof(msg), "+ Package %s has not been reviewed", pkg_name);
	return msg;
}

