#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libbds/bds_queue.h>

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

int pkg_vector_compar(const void *a, const void *b)
{
        const struct pkg *pa = *(const struct pkg **)a;
        const struct pkg *pb = *(const struct pkg **)b;

        if (pa->name == pb->name)
                return 0;

        if (pa->name == NULL)
                return 1;
        if (pb->name == NULL)
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

        if (pkg->sbo_dir)
                new_pkg->sbo_dir = bds_string_dup(pkg->sbo_dir);
        new_pkg->info_crc        = pkg->info_crc;

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
                pkg->dep.required = pkg_vector_alloc_reference();
        }

        if (pkg_vector_lsearch(pkg->dep.required, req->name))
                return;

        bds_vector_append(pkg->dep.required, &req);
}

void pkg_append_parent(struct pkg *pkg, struct pkg *parent)
{
        if (pkg->dep.parents == NULL) {
                pkg->dep.parents = pkg_vector_alloc_reference();
        }

        if (pkg_vector_lsearch(pkg->dep.parents, parent->name))
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

pkg_vector_t *pkg_vector_alloc_reference() { return bds_vector_alloc(1, sizeof(struct pkg *), NULL); }
pkg_vector_t *pkg_vector_alloc() { return bds_vector_alloc(1, sizeof(struct pkg *), (void (*)(void *))pkg_free); }

void pkg_vector_free(pkg_vector_t **pl) { bds_vector_free(pl); }

void pkg_vector_append(pkg_vector_t *pl, struct pkg *pkg) { bds_vector_append(pl, &pkg); }

void pkg_vector_insert_sort(pkg_vector_t *pkg_vector, struct pkg *pkg)
{
        pkg_vector_append(pkg_vector, pkg);

        const struct pkg **pkgp_begin = (const struct pkg **)bds_vector_ptr(pkg_vector);
        const struct pkg **pkgp       = pkgp_begin + bds_vector_size(pkg_vector) - 1;

        while (pkgp != pkgp_begin) {
                if (pkg_vector_compar(pkgp - 1, pkgp) <= 0) {
                        break;
                }

                const struct pkg *tmp = *(pkgp - 1);
                *(pkgp - 1)           = *pkgp;
                *pkgp                 = tmp;

                --pkgp;
        }
}

int pkg_vector_remove(pkg_vector_t *pl, const char *pkg_name)
{
        struct pkg key = {.name = (char *)pkg_name};
        struct pkg *keyp = &key;

        struct pkg **pkgp = (struct pkg **)bds_vector_lsearch(pl, &keyp, pkg_vector_compar);
        if (pkgp == NULL)
                return 1;

        pkg_destroy(*pkgp);
        bds_vector_qsort(pl, pkg_vector_compar);

        return 0;
}

struct pkg *pkg_vector_lsearch(pkg_vector_t *pl, const char *name)
{
        const struct pkg key = {.name = (char *)name};
        const struct pkg *keyp = &key;

        struct pkg **pkgp = (struct pkg **)bds_vector_lsearch(pl, &keyp, pkg_vector_compar);
        if (pkgp)
                return *pkgp;

        return NULL;
}

struct pkg *pkg_vector_bsearch(pkg_vector_t *pl, const char *name)
{
        const struct pkg key = {.name = (char *)name};
        const struct pkg *keyp = &key;

        struct pkg **pkgp = (struct pkg **)bds_vector_bsearch(pl, &keyp, pkg_vector_compar);
        if (pkgp)
                return *pkgp;

        return NULL;
}

struct pkg_graph *pkg_graph_alloc()
{
        struct pkg_graph *pkg_graph = calloc(1, sizeof(*pkg_graph));

        pkg_graph->sbo_pkgs  = pkg_vector_alloc();
        pkg_graph->meta_pkgs = pkg_vector_alloc();

	return pkg_graph;
}

struct pkg_graph *pkg_graph_alloc_reference()
{
        struct pkg_graph *pkg_graph = calloc(1, sizeof(*pkg_graph));

        pkg_graph->sbo_pkgs  = pkg_vector_alloc_reference();
        pkg_graph->meta_pkgs = pkg_vector_alloc_reference();

	return pkg_graph;
}

void pkg_graph_free(struct pkg_graph **pkg_graph)
{
        if (*pkg_graph == NULL)
                return;

        pkg_vector_free(&(*pkg_graph)->sbo_pkgs);
        pkg_vector_free(&(*pkg_graph)->meta_pkgs);

        free(*pkg_graph);
        *pkg_graph = NULL;
}

pkg_vector_t *pkg_graph_sbo_pkgs(struct pkg_graph *pkg_graph) { return pkg_graph->sbo_pkgs; }

pkg_vector_t *pkg_graph_meta_pkgs(struct pkg_graph *pkg_graph) { return pkg_graph->meta_pkgs; }

struct pkg *pkg_graph_search(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        struct pkg *pkg = NULL;

        if ((pkg = pkg_vector_bsearch(pkg_graph->sbo_pkgs, pkg_name)))
                return pkg;

	if ((pkg = pkg_vector_bsearch(pkg_graph->meta_pkgs, pkg_name)))
		return pkg;

        if (is_meta_pkg(pkg_name)) {
                pkg              = pkg_alloc(pkg_name);
		pkg->dep.is_meta = true;
                pkg_vector_insert_sort(pkg_graph->meta_pkgs, pkg);
		//load_dep_file(pkg_graph, pkg_name, options);				
        }

        return pkg;
}

static int __load_sbo(pkg_vector_t *pkgs, const char *cur_path)
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
                                rc             = __load_sbo(pkgs, next_dir);
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
                                pkg_vector_append(pkgs, pkg);
                        }
                }
        }

finish:
        if (dp)
                closedir(dp);
        --level;

        return rc;
}

int pkg_load_sbo(pkg_vector_t *pkgs)
{
        int rc = 0;
        if ((rc = __load_sbo(pkgs, user_config.sbopkg_repo)) != 0) {
                return rc;
        }
        bds_vector_qsort(pkgs, pkg_vector_compar);

        return 0;
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

static int __create_db(const char *db_file, pkg_vector_t *pkgs, bool write_sbo_dir)
{
        FILE *fp = fopen(db_file, "w");
        assert(fp);

        for (size_t i = 0; i < bds_vector_size(pkgs); ++i) {
                struct pkg *pkg = *(struct pkg **)bds_vector_get(pkgs, i);

                if (pkg->name == NULL) /* Package has been removed */
                        continue;

                fprintf(fp, "%s:0x%x", pkg->name, pkg->info_crc);
                if (write_sbo_dir)
                        fprintf(fp, ":%s", pkg->sbo_dir + strlen(user_config.sbopkg_repo) + 1);
                fprintf(fp, "\n");
        }
        fclose(fp);

        return 0;
}

int pkg_create_db(pkg_vector_t *pkgs)
{
        char db_file[4096];
        bds_string_copyf(db_file, sizeof(db_file), "%s/PKGDB", user_config.depdir);

        return __create_db(db_file, pkgs, true);
}

int pkg_create_reviewed(pkg_vector_t *pkgs)
{
        char db_file[4096];
        bds_string_copyf(db_file, sizeof(db_file), "%s/REVIEWED", user_config.depdir);

        return __create_db(db_file, pkgs, false);
}

static int __load_db(pkg_vector_t *pkgs, const char *db_file, bool read_sbo_dir)
{
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
                if (read_sbo_dir) {
                        assert(num_tok == 3);
                } else {
                        assert(num_tok == 2);
                }

                struct pkg *pkg = pkg_alloc(tok[0]);

                pkg->info_crc = strtol(tok[1], NULL, 16);

                if (read_sbo_dir) {
                        bds_string_copyf(sbo_dir, sizeof(sbo_dir), "%s/%s", user_config.sbopkg_repo, tok[2]);
                        pkg_init_sbo_dir(pkg, sbo_dir);
                }
                pkg_vector_append(pkgs, pkg);

                free(tok);
        }
        fclose(fp);

        return 0;
}

int pkg_load_db(pkg_vector_t *pkgs)
{
        if (!pkg_db_exists())
                return 1;

        char db_file[4096];
        bds_string_copyf(db_file, sizeof(db_file), "%s/PKGDB", user_config.depdir);

        return __load_db(pkgs, db_file, true);
}

int pkg_load_reviewed(pkg_vector_t *pkgs)
{
        if (!pkg_reviewed_exists())
                return 1;

        char db_file[4096];
        bds_string_copyf(db_file, sizeof(db_file), "%s/REVIEWED", user_config.depdir);

        return __load_db(pkgs, db_file, false);
}

int pkg_load_dep(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options options)
{
        return load_dep_file(pkg_graph, pkg_name, options);
}

int pkg_load_revdeps(struct pkg_graph *pkg_graph, struct pkg_options options)
{
        options.revdeps = true;

        pkg_vector_t *pkgs[2] = {pkg_graph->sbo_pkgs, pkg_graph->meta_pkgs};

        for (size_t n = 0; n < 2; ++n) {
                // We load deps for all packages
                for (size_t i = 0; i < bds_vector_size(pkgs[n]); ++i) {
                        struct pkg *pkg = *(struct pkg **)bds_vector_get(pkgs[n], i);

                        if (pkg->name == NULL)
                                continue;

			int rc = 0;
                        if ((rc = load_dep_file(pkg_graph, pkg->name, options)) != 0)
                                return rc;
                }
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

struct pkg_node pkg_node_create(struct pkg *pkg, int dist)
{
        struct pkg_node node = {.pkg = pkg, .dist = dist};
        return node;
}

void pkg_node_destroy(struct pkg_node *node) { memset(node, 0, sizeof(*node)); }

int pkg_node_compar(const void *a, const void *b)
{
        return strcmp(((const struct pkg_node *)a)->pkg->name, ((const struct pkg_node *)b)->pkg->name);
}

static int next_node_dist(struct pkg_node node)
{
	return node.dist + (node.pkg->dep.is_meta ? 0 : 1 );
}

struct pkg_node *pkg_iterator_begin(struct pkg_iterator *iter, struct pkg *pkg, enum pkg_iterator_type type,
				    int max_dist)
{
        memset(iter, 0, sizeof(*iter));

        iter->type     = type;
        iter->max_dist = max_dist;

        iter->pkgs = (type == ITERATOR_REQUIRED ? pkg->dep.required : pkg->dep.parents);
        if (iter->pkgs == NULL)
                return NULL;

        if (bds_vector_size(iter->pkgs) == 0)
                return NULL;

        iter->pkgs_index = -1;
        iter->pkgp  = (struct pkg **)bds_vector_get(iter->pkgs, 0);

        iter->next_queue = bds_queue_alloc(1, sizeof(struct pkg_node), (void (*)(void *))pkg_node_destroy);
        bds_queue_set_autoresize(iter->next_queue, true);

        iter->cur_node = pkg_node_create(pkg, 0);

        return pkg_iterator_next(iter);
}

struct pkg_node *pkg_iterator_current(struct pkg_iterator *iter) { return &iter->cur_node; }
struct pkg_node *pkg_iterator_node(struct pkg_iterator *iter) { return &iter->pkg_node; }

enum pkg_color {
	COLOR_WHITE=0, // 
	COLOR_GREY,
	COLOR_BLACK
};

struct pkg_node *pkg_iterator_next(struct pkg_iterator *iter)
{
        ++iter->pkgs_index;
        if (iter->pkgs_index == bds_vector_size(iter->pkgs)) {
                // Setup new pkg
                while (1) {
                        if (bds_queue_pop(iter->next_queue, &iter->cur_node) == NULL)
                                return NULL;

                        if (iter->max_dist >= 0)
                                assert(next_node_dist(iter->cur_node) <= iter->max_dist);
			
                        iter->pkgs = (iter->type == ITERATOR_REQUIRED ? iter->cur_node.pkg->dep.required
                                                                       : iter->cur_node.pkg->dep.parents);
                        if (iter->pkgs == NULL)
                                continue;
                        if (bds_vector_size(iter->pkgs) == 0)
                                continue;
			
                        iter->pkgs_index = 0;
                        iter->pkgp  = (struct pkg **)bds_vector_get(iter->pkgs, 0);
                        break;
                }
        }

	const int next_dist = next_node_dist(iter->cur_node);
	
        if (iter->max_dist < 0 || next_dist < iter->max_dist) {
                struct pkg_node next_node =
                    pkg_node_create(iter->pkgp[iter->pkgs_index], next_dist);
                // if (bds_queue_lsearch(iter->next_queue, &next_node, pkg_node_compar) == NULL) {
                bds_queue_push(iter->next_queue, &next_node);

                // }
        }

	pkg_node_destroy(&iter->pkg_node);
	iter->pkg_node = pkg_node_create(iter->pkgp[iter->pkgs_index], next_dist);
	
        // Skip meta pkgs	
	if( iter->pkg_node.pkg->dep.is_meta ) {
		pkg_iterator_next(iter);
	}
        return &iter->pkg_node;
}

void pkg_iterator_destroy(struct pkg_iterator *iter)
{
        if (iter->next_queue)
                bds_queue_free(&iter->next_queue);

        memset(iter, 0, sizeof(*iter));
}
