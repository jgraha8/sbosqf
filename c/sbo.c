#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libbds/bds_string.h>

#include "file_mmap.h"
#include "sbo.h"


const char *sbo_find_dir(const char *cur_path, const char *pkg_name)
{
        static char sbo_dir[4096];
        static int level = 0;

        const char *rp        = NULL;
        struct dirent *dirent = NULL;

        DIR *dp = opendir(cur_path);
        if (dp == NULL)
                return NULL;

        ++level;
        assert(level >= 1);

        if (level > 2) {
                goto finish;
        }

        while ((dirent = readdir(dp)) != NULL) {
                if (strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0)
                        continue;

                if (level == 1) {
                        if (dirent->d_type == DT_DIR) {
                                char *next_dir = bds_string_dup_concat(3, cur_path, "/", dirent->d_name);
                                rp             = sbo_find_dir(next_dir, pkg_name);
                                free(next_dir);
                                if (rp) {
                                        goto finish;
                                }
                        }
                } else {
                        if (dirent->d_type == DT_DIR) {
                                if (strcmp(dirent->d_name, pkg_name) == 0) {
                                        rp = bds_string_copyf(sbo_dir, 4096, "%s/%s", cur_path, dirent->d_name);
                                        goto finish;
                                }
                        }
                }
        }

finish:
        if (dp)
                closedir(dp);
        --level;

        return rp;
}

const char *sbo_find_info(const char *sbo_repo, const char *pkg_name)
{
        static char info_file[4096];
        const char *sbo_dir = sbo_find_dir(sbo_repo, pkg_name);

        if (!sbo_dir)
                return NULL;

        bds_string_copyf(info_file, sizeof(info_file), "%s/%s.info", sbo_dir, pkg_name);

        return info_file;
}

const char *sbo_find_readme(const char *sbo_repo, const char *pkg_name)
{
        static char readme_file[4096];
        const char *sbo_dir = sbo_find_dir(sbo_repo, pkg_name);

        if (!sbo_dir)
                return NULL;

        bds_string_copyf(readme_file, sizeof(readme_file), "%s/README", sbo_dir);

        return readme_file;
}

char *sbo_load_readme(const char *sbo_dir, const char *pkg_name)
{
	char *readme = NULL;
        static char readme_file[4096];	
	bds_string_copyf(readme_file, sizeof(readme_file), "%s/README", sbo_dir);

	struct file_mmap *readme_mmap = file_mmap(readme_file);

	if( readme_mmap != NULL ) {
		readme = bds_string_dup(readme_mmap->data);
		file_munmap(&readme_mmap);
	}
		

	return readme;
}

const char *sbo_read_requires(const char *sbo_dir, const char *pkg_name)
{
        static char sbo_requires[4096];
        const char *rp = NULL;

        char *info_file = bds_string_dup_concat(4, sbo_dir, "/", pkg_name, ".info");

        struct file_mmap *info;
        if ((info = file_mmap(info_file)) == NULL) {
                goto finish;
        }

        char *c = bds_string_find(info->data, "REQUIRES=");
        if (c == NULL) {
                goto finish;
        }
        char *str = c + strlen("REQUIRES=");

	// Find opening quote
	assert(c = bds_string_find(str, "\""));
	str = c+1;

        // Find closing quote
	assert(c = bds_string_find(str, "\""));
	*c = '\0';

	// Remove line continuation
        while ((c = bds_string_find(str, "\\"))) {
                *c = ' ';
        }
        strncpy(sbo_requires, bds_string_atrim(str), 4096);
        rp = sbo_requires;

finish:
        if (info_file)
                free(info_file);
        if (info)
                file_munmap(&info);

        return rp;
}


static int __search_sbo_repo(const char *pathname, const char *pkg_name, struct bds_vector *pkg_list)
{
        int rc           = 0;
        static int level = 0;

        DIR *dp               = NULL;
        struct dirent *dirent = NULL;

        ++level;
        assert(level >= 1);

        if (level > 2) {
                goto finish;
        }

        dp = opendir(pathname);
        if (dp == NULL) {
                rc = 1;
                goto finish;
        }

        while ((dirent = readdir(dp)) != NULL) {
                if (dirent->d_type != DT_DIR)
                        continue;

                if (strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0)
                        continue;

                if (level == 1) {
                        char *next_dir = bds_string_dup_concat(3, pathname, "/", dirent->d_name);
                        rc             = __search_sbo_repo(next_dir, pkg_name, pkg_list);
                        free(next_dir);
                        if (rc != 0) {
                                goto finish;
                        }
                } else {
                        char d_name[256];
                        strncpy(d_name, dirent->d_name, 255);

                        if (bds_string_contains(bds_string_tolower(d_name), pkg_name)) {
                                char *d = bds_string_rfind(pathname, "/");
                                assert(d);

                                char *p = bds_string_dup_concat(3, d + 1, "/", dirent->d_name);
                                bds_vector_append(pkg_list, &p);
                        }
                }
        }

finish:
        if (dp)
                closedir(dp);
        --level;

        return rc;
}

void free_string(void *str) { free(*(char **)str); }

int search_sbo_repo(const char *sbo_repo, const char *pkg_name, struct bds_vector **pkg_list)
{
        *pkg_list = NULL;

        char *__pkg_name              = bds_string_tolower(bds_string_dup(pkg_name));
        struct bds_vector *__pkg_list = bds_vector_alloc(1, sizeof(char *), free_string);

        int rc = 0;
        if ((rc = __search_sbo_repo(sbo_repo, __pkg_name, __pkg_list)) != 0) {
                bds_vector_free(&__pkg_list);
        } else {
                *pkg_list = __pkg_list;
        }
        free(__pkg_name);

        return rc;
}
