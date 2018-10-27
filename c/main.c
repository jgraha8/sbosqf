/**
 * @file
 * @brief Main program file for sbopkg-dep2sqf
 *
 */

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libbds/bds_string.h>

#include "config.h"
#include "depfile.h"
#include "pkglist.h"
#include "user_config.h"

int main(int argc, char **argv)
{
        struct user_config user_config = default_user_config();
        load_user_config(&user_config);

        printf("sbopkg_repo = %s\n", user_config.sbopkg_repo);
        printf("sbo_tag = %s\n", user_config.sbo_tag);
        printf("pager = %s\n", user_config.pager);

        struct bds_stack *pkglist = load_pkglist(DEPDIR);
        print_pkglist(pkglist);

	struct pkg *pkg = find_pkg(pkglist, "nextcloud-server");

	if( pkg ) {
		printf("found package %s\n", pkg->name);
	}

	write_depdb(DEPDIR, pkglist);
	
        bds_stack_free(&pkglist);

        struct dep *dep = load_depfile(DEPDIR, "ffmpeg", true);

        printf("===========================\n");
        print_depfile(dep);

        dep_free(&dep);

	destoy_user_config(&user_config);

        return 0;
}
