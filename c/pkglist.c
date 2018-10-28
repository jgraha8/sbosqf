#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libbds/bds_stack.h>
#include <libbds/bds_string.h>

#include "user_config.h"
#include "pkglist.h"

int compar_pkg(const void *a, const void *b)
{
        return strcmp(((const struct pkg *)a)->name, ((const struct pkg *)b)->name);
}

struct pkg create_pkg(const char *name)
{
        struct pkg pkg;
        memset(&pkg, 0, sizeof(pkg));

        pkg.name = bds_string_dup(name);

        return pkg;
}

void destroy_pkg(struct pkg *pkg)
{
        free(pkg->name);
        memset(pkg, 0, sizeof(*pkg));
}

struct pkg *find_pkg(pkg_stack_t *pkglist, const char *pkg_name)
{
        const struct pkg key = {.name = (char *)pkg_name};

        return (struct pkg *)bsearch(&key, bds_stack_ptr(pkglist), bds_stack_size(pkglist), sizeof(key),
                                     compar_pkg);
}

pkg_stack_t *load_pkglist(const char *depdir)
{
        pkg_stack_t *pkglist = bds_stack_alloc(1, sizeof(struct pkg), (void (*)(void *))destroy_pkg);

        char *pkglist_file = bds_string_dup_concat(2, depdir, "/" PKGLIST);
        FILE *fp           = fopen(pkglist_file, "r");

        if (fp == NULL) {
                fprintf(stderr, "%s(%d): unable to open %s\n", __FILE__, __LINE__, pkglist_file);
                exit(EXIT_FAILURE);
        }

        char *line       = NULL;
        size_t num_line  = 0;
        ssize_t num_read = 0;

        while ((num_read = getline(&line, &num_line, fp)) != -1) {
                assert(line);

                char *new_line = bds_string_rfind(line, "\n");
                if (new_line)
                        *new_line = '\0';

                if (*bds_string_atrim(line) == '\0') {
                        goto cycle;
                }

                struct pkg pkg = create_pkg(line);
                bds_stack_push(pkglist, &pkg);

        cycle:
                free(line);
                line     = NULL;
                num_line = 0;
        }
        if (line != NULL) {
                free(line);
        }

        bds_stack_qsort(pkglist, compar_pkg);

        fclose(fp);
        free(pkglist_file);

        return pkglist;
}

int write_pkglist(const char *depdir, const pkg_stack_t *pkglist)
{
        char *pkglist_file = bds_string_dup_concat(2, depdir, "/" PKGLIST);
        char *tmp_file     = bds_string_dup_concat(2, depdir, "/." PKGLIST);	
        FILE *fp           = fopen(tmp_file, "w");

        if (fp == NULL) {
		return 1;
        }

        const struct pkg *p = (const struct pkg *)bds_stack_ptr(pkglist);

        for (size_t i = 0; i < bds_stack_size(pkglist); ++i) {
                fprintf(fp, "%s\n", p[i].name);
        }

	if( fflush(fp) != 0 ) {
		perror("fflush");
		return 2;
	}
	if( fclose(fp) != 0 ) {
		perror("fclose");
		return 3;
	}

	if( rename(tmp_file, pkglist_file) != 0 ) {
		perror("rename");
		return 4;
	}
	
	return 0;
}

void print_pkglist(const pkg_stack_t *pkglist)
{
        const struct pkg *p = (const struct pkg *)bds_stack_ptr(pkglist);

        for (size_t i = 0; i < bds_stack_size(pkglist); ++i) {
                printf("%s\n", p[i].name);
        }
}

int request_pkg_add(pkg_stack_t *pkglist, const char *pkg_name)
{
	int rc=0;
	
	printf("Add package %s (y/n)? ", pkg_name);
	fflush(stdout);

	char response[2] = {0};
	if( fgets(response, 2, stdin) == NULL ) {
		rc = 1;
		goto finish;
	}

	if( *response != 'y' ) {
		rc = 2;
		goto finish;
	}
	
	/* const char *sbo_dir = find_sbo_dir(user_config.sbopkg_repo, pkg_name); */
	/* if( !sbo_dir ) { */
	/* 	rc = 3; */
	/* 	goto finish; */
	/* } */

	/* const char *sbo_requires = read_sbo_requires(sbo_dir, pkg_name); */
	/* if( !sbo_requires ) { */
	/* 	rc = 4; */
	/* 	goto finish; */
	/* } */

	struct pkg pkg = create_pkg(pkg_name);
	bds_stack_push(pkglist, &pkg);

	bds_stack_qsort(pkglist, compar_pkg);

finish:
	return rc;
	
}
