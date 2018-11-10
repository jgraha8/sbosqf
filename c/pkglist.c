#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libbds/bds_stack.h>
#include <libbds/bds_string.h>

#include "deps.h"
#include "file_mmap.h"
#include "filesystem.h"
#include "pkglist.h"
#include "response.h"
#include "user_config.h"

#define BORDER1 "================================================================================"
#define BORDER2 "::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::"
#define BORDER3 "--------------------------------------------------------------------------------"

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

pkg_stack_t *load_pkglist(const char *pkgdb)
{
        pkg_stack_t *pkglist = bds_stack_alloc(1, sizeof(struct pkg), (void (*)(void *))destroy_pkg);

        char *pkglist_file = bds_string_dup_concat(3, user_config.depdir, "/", pkgdb);
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

int write_pkglist(const pkg_stack_t *pkglist, const char *pkgdb)
{
        char *pkglist_file = bds_string_dup_concat(3, user_config.depdir, "/", pkgdb);
        char *tmp_file     = bds_string_dup_concat(3, user_config.depdir, "/.", pkgdb);
        FILE *fp           = fopen(tmp_file, "w");

        if (fp == NULL) {
                return 1;
        }

        const struct pkg *p = (const struct pkg *)bds_stack_ptr(pkglist);

        for (size_t i = 0; i < bds_stack_size(pkglist); ++i) {
                fprintf(fp, "%s\n", p[i].name);
        }

        if (fflush(fp) != 0) {
                perror("fflush");
                return 2;
        }
        if (fclose(fp) != 0) {
                perror("fclose");
                return 3;
        }

        if (rename(tmp_file, pkglist_file) != 0) {
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

int request_pkg_add(pkg_stack_t *pkglist, const char *pkgdb, const char *pkg_name)
{
        int rc = 0;

        if (find_pkg(pkglist, pkg_name) != NULL) {
                printf("package %s already present in %s\n", pkg_name, pkgdb);
                rc = 1;
                goto finish;
        }

        printf("Add package %s (y/n)? ", pkg_name);
        fflush(stdout);

        if (read_response() != 'y') {
                printf("not adding package %s\n", pkg_name);
                fflush(stdout);
                rc = 1;
                goto finish;
        }

        struct pkg pkg = create_pkg(pkg_name);
        bds_stack_push(pkglist, &pkg);

        bds_stack_qsort(pkglist, compar_pkg);

        rc = write_pkglist(pkglist, pkgdb);

finish:
        return rc;
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
                        "\n" BORDER2 "\n"
                        "README\n" BORDER2 "\n"
                        "%s\n" // readme file
                        "\n" BORDER2 "\n"
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
