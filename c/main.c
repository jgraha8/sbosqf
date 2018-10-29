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
#include "deps.h"
#include "pkglist.h"
#include "user_config.h"
#include "filesystem.h"

int main(int argc, char **argv)
{
	const bool recursive = true;
	const bool optional = true;

	init_user_config();
	
        //struct user_config user_config = default_user_config();
	
        load_user_config();

        printf("sbopkg_repo = %s\n", user_config.sbopkg_repo);
        printf("sbo_tag = %s\n", user_config.sbo_tag);
        printf("pager = %s\n", user_config.pager);

        pkg_stack_t *pkglist = load_pkglist(DEPDIR);
        print_pkglist(pkglist);

	struct pkg *pkg = find_pkg(pkglist, "nextcloud-server");

	if( pkg ) {
		printf("found package %s\n", pkg->name);
	}

	write_depdb(DEPDIR, pkglist, recursive, optional);
	

        struct dep *dep = load_dep_file(DEPDIR, "test", recursive, optional);

        printf("===========================\n");
        print_dep_sqf(dep);

        dep_free(&dep);

	printf("===========================\n");

	const char *pkg_name = "virt-manager";
	const char *sbo_dir = find_sbo_dir(user_config.sbopkg_repo, pkg_name);

	if( sbo_dir ) {
		printf("found %s package directory %s\n", pkg_name, sbo_dir );

		const char *sbo_requires = read_sbo_requires(sbo_dir, pkg_name);
		if( sbo_requires ) {
			printf("  %s requires: %s\n", pkg_name, sbo_requires);
		}
	} else {
		printf("unable to find ffmpeg package directory\n");
	}

	write_parentdb(DEPDIR, pkglist, recursive, optional);

	if( request_pkg_add(pkglist, "junk") == 0 ) 
		write_pkglist(DEPDIR, pkglist);
	
        bds_stack_free(&pkglist);
	//destoy_user_config(&user_config);

	printf("===========================\n");
	printf("Creating dep file for wayland-protocols\n");
	printf("===========================\n");
	if( create_default_dep_file("wayland-protocols") != 0 ) {
		fprintf(stderr, "unable to create wayland-protocols dep file\n");
	}

	const char *slack_pkg_name = "xorg-server-xephyr";
	
	printf("===========================\n");
	printf("Checking if %s is installed\n", slack_pkg_name);
	printf("===========================\n");
	if( is_pkg_installed( slack_pkg_name, NULL) ) {
		printf("%s is installed\n", slack_pkg_name);
	} else {
		fprintf(stderr, "%s is not installed\n", slack_pkg_name);
	}
	
	destroy_user_config();
        return 0;
}
