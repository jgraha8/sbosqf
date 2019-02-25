#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "file_mmap.h"
#include "pkg.h"
#include "pkg_util.h"
#include "sbo.h"
#include "slack_pkg.h"
#include "user_config.h"

#ifndef MAX_LINE
#define MAX_LINE 2048
#endif

#define BORDER1 "================================================================================"
#define BORDER2 "::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::"
#define BORDER3 "--------------------------------------------------------------------------------"

static void free_string_ptr(char **str)
{
        if (*str == NULL)
                return;
        free(*str);
        *str = NULL;
}

int compar_pkg_list(const void *a, const void *b)
{
        const struct pkg *pa = *(const struct pkg **)a;
        const struct pkg *pb = *(const struct pkg **)b;

	if( pa->name == pb->name )
		return 0;

	if( pa->name == NULL )
		return 1;
	if( pb->name == NULL )
		return -1;
	
        return strcmp(pa->name, pb->name);
}

struct pkg_options pkg_options_default()
{
        struct pkg_options options;
        memset(&options, 0, sizeof(options));

        options.recursive = true;
        options.optional  = true;

        return options;
}

void pkg_destroy(struct pkg *pkg)
{
        if (pkg->name)
                free(pkg->name);
        if (pkg->sbo_dir)
                free(pkg->sbo_dir);

        if (pkg->dep.required) {
                bds_vector_free(&pkg->dep.required);
        }
        if (pkg->dep.parents) {
                bds_vector_free(&pkg->dep.parents);
        }
        if (pkg->dep.buildopts)
                bds_vector_free(&pkg->dep.buildopts);

        memset(pkg, 0, sizeof(*pkg));
}

struct pkg *pkg_alloc(const char *name)
{
        struct pkg *pkg = calloc(1, sizeof(*pkg));

        pkg->name = bds_string_dup(name);

        return pkg;
}

void pkg_free(struct pkg **pkg)
{
        if (*pkg == NULL)
                return;

        pkg_destroy(*pkg);
        free(*pkg);
        *pkg = NULL;
}

struct pkg *pkg_clone_nodep(const struct pkg *pkg)
{
	struct pkg *new_pkg = pkg_alloc(pkg->name);

	if( pkg->sbo_dir)
		new_pkg->sbo_dir = bds_string_dup(pkg->sbo_dir);
	new_pkg->info_crc = pkg->info_crc;

	return new_pkg;
}

void pkg_init_sbo_dir(struct pkg *pkg, const char *sbo_dir) { pkg->sbo_dir = bds_string_dup(sbo_dir); }

int pkg_set_info_crc(struct pkg *pkg)
{
        if (pkg->sbo_dir == NULL)
                return 1;

        char *readme         = sbo_load_readme(pkg->sbo_dir, pkg->name);
        const char *requires = sbo_read_requires(pkg->sbo_dir, pkg->name);

        pkg->info_crc = crc32_z(0L, Z_NULL, 0);
        pkg->info_crc = crc32_z(pkg->info_crc, (unsigned char *)readme, strlen(readme));
        pkg->info_crc = crc32_z(pkg->info_crc, (unsigned char *)requires, strlen(requires));

        free(readme);

        return 0;
}

void pkg_append_required(struct pkg *pkg, struct pkg *req)
{
        if (pkg->dep.required == NULL) {
                pkg->dep.required = pkg_list_alloc_reference();
        }

        if (pkg_list_lsearch(pkg->dep.required, req->name))
                return;

        bds_vector_append(pkg->dep.required, &req);
}

void pkg_append_parent(struct pkg *pkg, struct pkg *parent)
{
        if (pkg->dep.parents == NULL) {
                pkg->dep.parents = pkg_list_alloc_reference();
        }

        if (pkg_list_lsearch(pkg->dep.parents, parent->name))
                return;

        bds_vector_append(pkg->dep.parents, &parent);
}

void pkg_append_buildopts(struct pkg *pkg, char *bopt)
{
        if (pkg->dep.buildopts == NULL) {
                pkg->dep.buildopts = bds_vector_alloc(1, sizeof(char *), (void (*)(void *))free_string_ptr);
        }
        bds_vector_append(pkg->dep.buildopts, &bopt);
}

pkg_list_t *pkg_list_alloc_reference() { return bds_vector_alloc(1, sizeof(struct pkg *), NULL); }
pkg_list_t *pkg_list_alloc() { return bds_vector_alloc(1, sizeof(struct pkg *), (void (*)(void *))pkg_free); }

void pkg_list_free(pkg_list_t **pl) { bds_vector_free(pl); }

void pkg_list_append(pkg_list_t *pl, struct pkg *pkg) { bds_vector_append(pl, &pkg); }

int pkg_list_remove(pkg_list_t *pl, const char *pkg_name)
{
	struct pkg key = { .name = (char *)pkg_name };
	struct pkg *keyp = &key;
	
	struct pkg **pkgp = (struct pkg **)bds_vector_lsearch(pl, &keyp, compar_pkg_list);
	if( pkgp == NULL )
		return 1;

	pkg_destroy(*pkgp);
	bds_vector_qsort(pl, compar_pkg_list);

	return 0;
}

struct pkg *pkg_list_lsearch(pkg_list_t *pl, const char *name)
{
        const struct pkg key = {.name = (char *)name};
        const struct pkg *keyp = &key;

        struct pkg **pkgp = (struct pkg **)bds_vector_lsearch(pl, &keyp, compar_pkg_list);
        if (pkgp)
                return *pkgp;

        return NULL;
}

struct pkg *pkg_list_bsearch(pkg_list_t *pl, const char *name)
{
        const struct pkg key = {.name = (char *)name};
        const struct pkg *keyp = &key;

        struct pkg **pkgp = (struct pkg **)bds_vector_bsearch(pl, &keyp, compar_pkg_list);
        if (pkgp)
                return *pkgp;

        return NULL;
}

static int __load_sbo(pkg_list_t *pkg_list, const char *cur_path)
{
        static char sbo_dir[4096];
        static int level = 0;

        int rc                = 0;
        struct dirent *dirent = NULL;

        DIR *dp = opendir(cur_path);
        if (dp == NULL)
                return 1;

        ++level;
        assert(level >= 1);

        if (level > 2) {
                goto finish;
        }

        while ((dirent = readdir(dp)) != NULL) {
                if (*dirent->d_name == '.')
                        continue;

                if (level == 1) {
                        if (dirent->d_type == DT_DIR) {
                                char *next_dir = bds_string_dup_concat(3, cur_path, "/", dirent->d_name);
                                rc             = __load_sbo(pkg_list, next_dir);
                                free(next_dir);
                                if (rc != 0)
                                        goto finish;
                        }
                } else {
                        if (dirent->d_type == DT_DIR) {
                                assert(bds_string_copyf(sbo_dir, 4096, "%s/%s", cur_path, dirent->d_name));

                                // Now check for a .info file
                                char info[256];
                                bds_string_copyf(info, sizeof(info), "%s/%s.info", sbo_dir, dirent->d_name);

                                struct stat sb;
                                if (stat(info, &sb) != 0) {
                                        perror("stat()");
                                        continue;
                                }
                                if (!S_ISREG(sb.st_mode))
                                        continue;

                                struct pkg *pkg = pkg_alloc(dirent->d_name);

                                pkg_init_sbo_dir(pkg, sbo_dir);
                                pkg_set_info_crc(pkg);
                                pkg_list_append(pkg_list, pkg);
                        }
                }
        }

finish:
        if (dp)
                closedir(dp);
        --level;

        return rc;
}

pkg_list_t *pkg_load_sbo()
{
        pkg_list_t *pkg_list = pkg_list_alloc();

        if (__load_sbo(pkg_list, user_config.sbopkg_repo) != 0) {
                pkg_list_free(&pkg_list);
                return NULL;
        }
        bds_vector_qsort(pkg_list, compar_pkg_list);

        return pkg_list;
}

static bool file_exists(const char *filen)
{
        struct stat sb;

        if (stat(filen, &sb) == -1)
                return false;

        if (!S_ISREG(sb.st_mode))
                return false;

        return true;

}

bool pkg_db_exists()
{
        char db_file[MAX_LINE];
        bds_string_copyf(db_file, sizeof(db_file), "%s/PKGDB", user_config.depdir);

	return file_exists(db_file);
}

bool pkg_reviewed_exists()
{
        char db_file[4096];
        bds_string_copyf(db_file, sizeof(db_file), "%s/REVIEWED", user_config.depdir);

        return file_exists(db_file);
}

static int __create_db(const char *db_file, pkg_list_t *pkg_list, bool write_sbo_dir)
{
        FILE *fp = fopen(db_file, "w");
        assert(fp);
	
        for (size_t i = 0; i < bds_vector_size(pkg_list); ++i) {
                struct pkg *pkg = *(struct pkg **)bds_vector_get(pkg_list, i);

		if( pkg->name == NULL ) /* Package has been removed */
			continue;

		fprintf(fp, "%s:0x%x", pkg->name, pkg->info_crc);
		if( write_sbo_dir)
			fprintf(fp, ":%s", pkg->sbo_dir + strlen(user_config.sbopkg_repo) + 1);
		fprintf(fp, "\n");
        }
        fclose(fp);

	return 0;
}

int pkg_create_db(pkg_list_t *pkg_list)
{
        char db_file[4096];
        bds_string_copyf(db_file, sizeof(db_file), "%s/PKGDB", user_config.depdir);

	return __create_db(db_file, pkg_list, true);
}

int pkg_create_reviewed(pkg_list_t *pkg_list)
{
        char db_file[4096];
        bds_string_copyf(db_file, sizeof(db_file), "%s/REVIEWED", user_config.depdir);

	return __create_db(db_file, pkg_list, false);
}

static pkg_list_t *__load_db(const char *db_file, bool read_sbo_dir)
{
        pkg_list_t *pkg_list = pkg_list_alloc();

        FILE *fp = fopen(db_file, "r");
        assert(fp);

        char sbo_dir[256];
        char line[MAX_LINE];
        line[MAX_LINE - 1] = '\0';

        while (fgets(line, MAX_LINE, fp)) {
                char *c = NULL;
                // Get newline character
                if ((c = bds_string_rfind(line, "\n"))) {
                        *c = '\0';
                }
                bds_string_atrim(line);

                size_t num_tok = 0;
                char **tok     = NULL;

                bds_string_tokenize(line, ":", &num_tok, &tok);
		if( read_sbo_dir ) {
			assert(num_tok == 3);
		} else {
			assert(num_tok == 2);
		}

                struct pkg *pkg = pkg_alloc(tok[0]);

                pkg->info_crc = strtol(tok[1], NULL, 16);

		if( read_sbo_dir ) {
			bds_string_copyf(sbo_dir, sizeof(sbo_dir), "%s/%s", user_config.sbopkg_repo, tok[2]);
			pkg_init_sbo_dir(pkg, sbo_dir);
		}
                pkg_list_append(pkg_list, pkg);

                free(tok);
        }
        fclose(fp);

        return pkg_list;
}


pkg_list_t *pkg_load_db()
{
        if (!pkg_db_exists())
                return NULL;

        char db_file[4096];
        bds_string_copyf(db_file, sizeof(db_file), "%s/PKGDB", user_config.depdir);

	return __load_db(db_file, true);
}

pkg_list_t *pkg_load_reviewed()
{
        if (!pkg_reviewed_exists())
                return NULL;

        char db_file[4096];
        bds_string_copyf(db_file, sizeof(db_file), "%s/REVIEWED", user_config.depdir);

	return __load_db(db_file, false);
}

int pkg_load_dep(pkg_list_t *pkg_list, const char *pkg_name, struct pkg_options options)
{
        return load_dep_file(pkg_list, pkg_name, options);
}

int pkg_load_revdeps(pkg_list_t *pkg_list, struct pkg_options options)
{
	options.revdeps = true;
	
	// We load deps for all packages
	for( size_t i=0; i<bds_vector_size(pkg_list); ++i ) {
		struct pkg *pkg = *(struct pkg **)bds_vector_get(pkg_list, i);
		
		if( pkg->name == NULL )
			continue;

		if( load_dep_file(pkg_list, pkg->name, options) != 0 )
			return 1;
	}

	return 0;
	
}

int pkg_review(struct pkg *pkg)
{
        const char *sbo_info = sbo_find_info(user_config.sbopkg_repo, pkg->name);
        if (!sbo_info) {
                return -1;
        }

        FILE *fp = popen(user_config.pager, "w");

        if (!fp) {
                perror("popen()");
                return -1;
        }

        char file_name[MAX_LINE];

        struct file_mmap *readme = NULL;
        struct file_mmap *info   = NULL;
        struct file_mmap *dep    = NULL;

        bds_string_copyf(file_name, sizeof(file_name), "%s/README", pkg->sbo_dir);
        readme = file_mmap(file_name);
        if (!readme)
                goto finish;

        bds_string_copyf(file_name, sizeof(file_name), "%s/%s.info", pkg->sbo_dir, pkg->name);
        info = file_mmap(file_name);
        if (!info)
                goto finish;

        bds_string_copyf(file_name, sizeof(file_name), "%s/%s", user_config.depdir, pkg->name);
        if ((dep = file_mmap(file_name)) == NULL) {
                create_default_dep(pkg);
                assert(dep = file_mmap(file_name));
        }

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
                pkg->name, readme->data, pkg->name, info->data);

        fprintf(fp,
                BORDER1 "\n"
                        "%s\n" // package name
                BORDER1 "\n",
                pkg->name);

        if (dep) {
                fprintf(fp, "%s\n\n", dep->data);
        } else {
                fprintf(fp, "%s dependency file not found\n\n", pkg->name);
        }

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
