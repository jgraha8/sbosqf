#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libbds/bds_list.h>
#include <libbds/bds_string.h>

#include "deps.h"
#include "file_mmap.h"
#include "filesystem.h"
#include "pkg_db.h"
#include "response.h"
#include "user_config.h"

#define BORDER1 "================================================================================"
#define BORDER2 "::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::"
#define BORDER3 "--------------------------------------------------------------------------------"

pkg_list_t *pkg_db_pkglist = NULL;
pkg_list_t *pkg_db_reviewed = NULL;

static bool initd = false;
void init_pkg_db()
{
	assert(pkg_db_pkglist = load_pkg_db(PKGLIST));
	assert(pkg_db_reviewed = load_pkg_db(REVIEWED));

	initd = true;
}

void fini_pkg_db()
{
	if( !initd )
		return;
	
	bds_list_free(&pkg_db_pkglist);
	bds_list_free(&pkg_db_reviewed);

	initd = false;
}

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

struct pkg *find_pkg(pkg_list_t *pkg_db, const char *pkg_name)
{
        const struct pkg key = {.name = (char *)pkg_name};
        return (struct pkg *)bds_list_lsearch(pkg_db, &key, compar_pkg);
}

pkg_list_t *load_pkg_db(const char *db_name)
{
        pkg_list_t *pkg_db = bds_list_alloc(sizeof(struct pkg), (void (*)(void *))destroy_pkg);

        char *pkg_db_file = bds_string_dup_concat(3, user_config.depdir, "/", db_name);
        FILE *fp           = fopen(pkg_db_file, "r");

        if (fp == NULL) {
		// Does not exist
		return pkg_db;
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
		bds_list_insert_sort(pkg_db, &pkg, compar_pkg);

        cycle:
                free(line);
                line     = NULL;
                num_line = 0;
        }
        if (line != NULL) {
                free(line);
        }

        fclose(fp);
        free(pkg_db_file);

        return pkg_db;
}

int write_pkg_db(const pkg_list_t *pkg_db, const char *db_name)
{
        char *pkg_db_file = bds_string_dup_concat(3, user_config.depdir, "/", db_name);
        char *tmp_file     = bds_string_dup_concat(3, user_config.depdir, "/.", db_name);
        FILE *fp           = fopen(tmp_file, "w");

        if (fp == NULL) {
                return 1;
        }

        struct bds_list_node *node = bds_list_begin((pkg_list_t *)pkg_db);

	while( node != bds_list_end() ) {
		const struct pkg *pkg = (const struct pkg *)bds_list_object(node);
                fprintf(fp, "%s\n", pkg->name);

		node = bds_list_iterate(node);
        }

        if (fflush(fp) != 0) {
                perror("fflush");
                return 2;
        }
        if (fclose(fp) != 0) {
                perror("fclose");
                return 3;
        }

        if (rename(tmp_file, pkg_db_file) != 0) {
                perror("rename");
                return 4;
        }

        return 0;
}

void print_pkg_db(const pkg_list_t *pkg_db)
{
        struct bds_list_node *node = bds_list_begin((pkg_list_t *)pkg_db);

	while( node != bds_list_end() ) {
		const struct pkg *pkg = (const struct pkg *)bds_list_object(node);
                printf("%s\n", pkg->name);

		node = bds_list_iterate(node);
        }
}

int add_pkg(pkg_list_t *pkg_db, const char *db_name, const char *pkg_name)
{
        struct pkg pkg = create_pkg(pkg_name);
	
        bds_list_insert_sort(pkg_db, &pkg, compar_pkg);

        return write_pkg_db(pkg_db, db_name);
}

int remove_pkg(pkg_list_t *pkg_db, const char *db_name, const char *pkg_name)
{
	struct pkg pkg = { .name = (char *)pkg_name };

	if( find_pkg(pkg_db, pkg_name) ) {
		bds_list_remove(pkg_db, &pkg, compar_pkg);
		return write_pkg_db(pkg_db, db_name);
	}

	return 0;
}

int request_add_pkg(pkg_list_t *pkg_db, const char *db_name, const char *pkg_name)
{
        int rc = 0;

        if (find_pkg(pkg_db, pkg_name) != NULL) {
                printf("package %s already present in %s\n", pkg_name, db_name);
                rc = 1;
                goto finish;
        }

        printf("Add package %s to %s (y/n)? ", pkg_name, db_name);

        if (read_response() != 'y') {
                printf("not adding package %s\n", pkg_name);
                rc = 1;
                goto finish;
        }

        rc = add_pkg(pkg_db, db_name, pkg_name);

finish:
        return rc;
}

int request_review_pkg(const char *pkg_name)
{
        printf("Review package %s (y/n)? ", pkg_name);

        if (read_response() != 'y') {
                printf("not reviewing package %s\n", pkg_name);
                return 1;
        }

        return review_pkg(pkg_name);
}

int review_pkg(const char *pkg_name)
{
        const char *sbo_info = find_sbo_info(user_config.sbopkg_repo, pkg_name);
        if (!sbo_info) {
                return -1;
        }

        FILE *fp = popen(user_config.pager, "w");

        if (!fp) {
                perror("popen()");
                return -1;
        }

        const char *readme_file = NULL;
        const char *info_file   = NULL;
        const char *dep_file    = NULL;

        struct file_mmap *readme = NULL;
        struct file_mmap *info   = NULL;
        struct file_mmap *dep    = NULL;

        readme_file = find_sbo_readme(user_config.sbopkg_repo, pkg_name);
        if (!readme_file)
                goto finish;

        info_file = find_sbo_info(user_config.sbopkg_repo, pkg_name);
        if (!info_file)
                goto finish;

        readme = file_mmap(readme_file);
        if (!readme)
                goto finish;

        info = file_mmap(info_file);
        if (!info)
                goto finish;

        dep_file = find_dep_file(pkg_name);
        if (dep_file)
                dep = file_mmap(dep_file);

        // clang-format: off
        fprintf(fp,
                BORDER1 "\n"
                        "%s\n" // package name
                BORDER1 "\n"
                        "\n"
		BORDER2 "\n"
                        "README\n"
		BORDER2 "\n"
                        "%s\n" // readme file
                        "\n"
		BORDER2 "\n"
                        "%s.info\n" // package name (info)
                BORDER2 "\n"
                        "%s\n" // package info
                        "\n",
                pkg_name, readme->data, pkg_name, info->data);

        fprintf(fp,
                BORDER1 "\n"
                        "%s\n" // package name
                BORDER1 "\n",
                pkg_name);

        if (dep)
                fprintf(fp, "%s\n\n", dep->data);

finish:
        if (fp)
                if (pclose(fp) == -1) {
                        perror("pclose()");
                }
        if (readme)
                file_munmap(&readme);
        if (info)
                file_munmap(&info);
        if (dep)
                file_munmap(&dep);

        return 0;
}
