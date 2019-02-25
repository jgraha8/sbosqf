#include <assert.h>
#include <stdio.h>

#include <libbds/bds_vector.h>

#include "pkg.h"

#include "config.h"
#include "user_config.h"

void print_revdeps(struct pkg *pkg, struct pkg_options options)
{
        static pkg_list_t *parents_list = NULL;
        static bool initd               = false;
        static int level                = 0;

        if (pkg->dep.parents == NULL)
                return;

        ++level;

        if (!initd) {
                parents_list = pkg_list_alloc_reference();
                initd        = true;
        }

        for (size_t i = 0; i < bds_vector_size(pkg->dep.parents); ++i) {
                struct pkg *p = *(struct pkg **)bds_vector_get(pkg->dep.parents, i);

                if (bds_vector_lsearch(parents_list, &p, compar_pkg_list) == NULL) {
                        bds_vector_append(parents_list, &p);
                /* } */

                /* if( options.recursive ) { */
                /* 	for (size_t i = 0; i < bds_vector_size(pkg->dep.parents); ++i) { */
                /* 		struct pkg *p = *(struct pkg **)bds_vector_get(pkg->dep.parents, i); */
			print_revdeps(p, options);
		}
        }

        if (level == 1) {
                for (size_t i = 0; i < bds_vector_size(parents_list); ++i) {
                        struct pkg *p = *(struct pkg **)bds_vector_get(parents_list, i);

                        printf("%s ", p->name);
                }

                pkg_list_free(&parents_list);
                initd = false;
        }

        --level;
}

int main(int argc, char **argv)
{
        pkg_list_t *pkg_list       = NULL;
        struct pkg_options options = pkg_options_default();

        user_config_init();

        if ((pkg_list = pkg_load_db()) == NULL) {
                pkg_list = pkg_load_sbo();
                pkg_create_db(pkg_list);
        }

        /* for( size_t i=0; i<bds_vector_size(pkg_list); ++i ) { */
        /* 	struct pkg *pkg = *(struct pkg **)bds_vector_get(pkg_list,i); */

        /* 	printf("pkg: name = %s, info_crc = 0x%x\n", pkg->name, pkg->info_crc); */
        /* } */

        //        pkg_load_dep(pkg_list, "virt-manager", options);

        pkg_load_revdeps(pkg_list, options);

        struct pkg *pkg = pkg_list_bsearch(pkg_list, "libvirt");

        pkg_review(pkg);

        pkg_list_t *reviewed_list = pkg_list_alloc_reference();

        pkg_list_append(reviewed_list, pkg);
        pkg_create_reviewed(reviewed_list);

        printf("%s revdeps:", pkg->name);

        print_revdeps(pkg, options);
        printf("\n");

        pkg_list_free(&pkg_list);
        pkg_list_free(&reviewed_list);
        user_config_destroy();

        return 0;
}
