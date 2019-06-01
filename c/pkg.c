#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libbds/bds_stack.h>

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

int pkg_nodes_compar(const void *a, const void *b)
{
        const struct pkg_node *pa = *(const struct pkg_node **)a;
        const struct pkg_node *pb = *(const struct pkg_node **)b;

        if (pa->pkg.name == pb->pkg.name)
                return 0;

        if (pa->pkg.name == NULL)
                return 1;
        if (pb->pkg.name == NULL)
                return -1;

        return strcmp(pa->pkg.name, pb->pkg.name);
}

struct pkg_options pkg_options_default()
{
        struct pkg_options options;
        memset(&options, 0, sizeof(options));

        options.recursive = true;
        options.optional  = true;

        return options;
}

struct pkg pkg_create(const char *name)
{
        struct pkg pkg;

        memset(&pkg, 0, sizeof(pkg));

        pkg.name = bds_string_dup(name);

        return pkg;
}

/* struct pkg pkg_create_reference(const struct pkg *pkg_src) */
/* { */
/* 	struct pkg pkg_dst; */

/* 	pkg_dst = *pkg_src; */
/* 	pkg_dst.owner = false; */
/* } */

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

/* void pkg_copy_nodep(struct pkg *pkg_dst, const struct pkg *pkg_src) */
/* { */
/*         pkg_destroy(pkg_dst); */

/*         if (pkg_src->sbo_dir) */
/*                 pkg_dst->sbo_dir = bds_string_dup(pkg_src->sbo_dir); */
/*         pkg_dst->info_crc        = pkg_src->info_crc; */

/*         return; */
/* } */

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
                pkg->dep.required = pkg_nodes_alloc_reference();
        }

        if (pkg_nodes_lsearch(pkg->dep.required, req->name))
                return;

        bds_vector_append(pkg->dep.required, &req);
}

void pkg_append_parent(struct pkg *pkg, struct pkg *parent)
{
        if (pkg->dep.parents == NULL) {
                pkg->dep.parents = pkg_nodes_alloc_reference();
        }

        if (pkg_nodes_lsearch(pkg->dep.parents, parent->name))
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

/*
  pkg_node
 */
struct pkg_node *pkg_node_alloc(const char *name)
{
        struct pkg_node *pkg_node = calloc(1, sizeof(*pkg_node));

        pkg_node->pkg = pkg_create(name);

        return pkg_node;
}

void pkg_node_destroy(struct pkg_node *pkg_node)
{
        pkg_destroy(&pkg_node->pkg);
        memset(&pkg_node, 0, sizeof(pkg_node));
}

void pkg_node_free(struct pkg_node **pkg_node)
{
        if (*pkg_node == NULL)
                return;

        pkg_node_destroy(*pkg_node);
        free(*pkg_node);
        *pkg_node = NULL;
}

pkg_nodes_t *pkg_nodes_alloc_reference() { return bds_vector_alloc(1, sizeof(struct pkg_node *), NULL); }
pkg_nodes_t *pkg_nodes_alloc()
{
        return bds_vector_alloc(1, sizeof(struct pkg_node *), (void (*)(void *))pkg_node_free);
}

void pkg_nodes_free(pkg_nodes_t **pl) { bds_vector_free(pl); }


const struct pkg_node *pkg_nodes_get_const(const pkg_nodes_t *nodes, size_t i)
{
	struct pkg_node **nodep = (struct pkg_node **)bds_vector_get(nodes, i);
	
	if( nodep == NULL )
		return NULL;
	
	return *nodep;
}

size_t pkg_nodes_size(const pkg_nodes_t *nodes)
{
	return bds_vector_size(nodes);
}

void pkg_nodes_append(pkg_nodes_t *pl, struct pkg_node *pkg_node) { bds_vector_append(pl, &pkg_node); }

void pkg_nodes_insert_sort(pkg_nodes_t *pkg_nodes, struct pkg_node *pkg_node)
{
        pkg_nodes_append(pkg_nodes, pkg_node);

        const struct pkg_node **pkgp_begin = (const struct pkg_node **)bds_vector_ptr(pkg_nodes);
        const struct pkg_node **pkgp       = pkgp_begin + bds_vector_size(pkg_nodes) - 1;

        while (pkgp != pkgp_begin) {
                if (pkg_nodes_compar(pkgp - 1, pkgp) <= 0) {
                        break;
                }

                const struct pkg_node *tmp = *(pkgp - 1);
                *(pkgp - 1)                = *pkgp;
                *pkgp                      = tmp;

                --pkgp;
        }
}

int pkg_nodes_remove(pkg_nodes_t *pl, const char *pkg_name)
{
        struct pkg_node key;
        struct pkg_node *keyp = &key;

        key.pkg.name = (char *)pkg_name;

        struct pkg_node **pkgp = (struct pkg_node **)bds_vector_lsearch(pl, &keyp, pkg_nodes_compar);
        if (pkgp == NULL)
                return 1;

        pkg_node_destroy(*pkgp);
        bds_vector_qsort(pl, pkg_nodes_compar);

        return 0;
}

struct pkg_node *pkg_nodes_lsearch(pkg_nodes_t *pl, const char *name)
{
        struct pkg_node key;
        const struct pkg_node *keyp = &key;

        key.pkg.name = (char *)name;

        struct pkg_node **pkgp = (struct pkg_node **)bds_vector_lsearch(pl, &keyp, pkg_nodes_compar);
        if (pkgp)
                return *pkgp;

        return NULL;
}

struct pkg_node *pkg_nodes_bsearch(pkg_nodes_t *pl, const char *name)
{
        struct pkg_node key;
        const struct pkg_node *keyp = &key;

        key.pkg.name = (char *)name;

        struct pkg_node **pkgp = (struct pkg_node **)bds_vector_bsearch(pl, &keyp, pkg_nodes_compar);
        if (pkgp)
                return *pkgp;

        return NULL;
}

struct pkg_graph *pkg_graph_alloc()
{
        struct pkg_graph *pkg_graph = calloc(1, sizeof(*pkg_graph));

        pkg_graph->sbo_pkgs  = pkg_nodes_alloc();
        pkg_graph->meta_pkgs = pkg_nodes_alloc();

        return pkg_graph;
}

struct pkg_graph *pkg_graph_alloc_reference()
{
        struct pkg_graph *pkg_graph = calloc(1, sizeof(*pkg_graph));

        pkg_graph->sbo_pkgs  = pkg_nodes_alloc_reference();
        pkg_graph->meta_pkgs = pkg_nodes_alloc_reference();

        return pkg_graph;
}

void pkg_graph_free(struct pkg_graph **pkg_graph)
{
        if (*pkg_graph == NULL)
                return;

        pkg_nodes_free(&(*pkg_graph)->sbo_pkgs);
        pkg_nodes_free(&(*pkg_graph)->meta_pkgs);

        free(*pkg_graph);
        *pkg_graph = NULL;
}

pkg_nodes_t *pkg_graph_sbo_pkgs(struct pkg_graph *pkg_graph) { return pkg_graph->sbo_pkgs; }

pkg_nodes_t *pkg_graph_meta_pkgs(struct pkg_graph *pkg_graph) { return pkg_graph->meta_pkgs; }

struct pkg_node *pkg_graph_search(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        struct pkg_node *pkg_node = NULL;

        if ((pkg_node = pkg_nodes_bsearch(pkg_graph->sbo_pkgs, pkg_name)))
                return pkg_node;

        if ((pkg_node = pkg_nodes_bsearch(pkg_graph->meta_pkgs, pkg_name)))
                return pkg_node;

        if (is_meta_pkg(pkg_name)) {
                pkg_node                  = pkg_node_alloc(pkg_name);
                pkg_node->pkg.dep.is_meta = true;
                pkg_nodes_insert_sort(pkg_graph->meta_pkgs, pkg_node);
                // load_dep_file(pkg_graph, pkg_name, options);
        }

        return pkg_node;
}

void pkg_node_clear_markers(struct pkg_node *pkg_node)
{
        pkg_node->dist       = 0;
        pkg_node->color      = COLOR_WHITE;
        pkg_node->edge_index = 0;
}

void pkg_graph_clear_markers(struct pkg_graph *pkg_graph)
{
        size_t n = 0;

        n = bds_vector_size(pkg_graph->sbo_pkgs);
        for (size_t i = 0; i < n; ++i) {
                struct pkg_node *pkg_node = *(struct pkg_node **)bds_vector_get(pkg_graph->sbo_pkgs, i);
                pkg_node_clear_markers(pkg_node);
        }

        n = bds_vector_size(pkg_graph->meta_pkgs);
        for (size_t i = 0; i < n; ++i) {
                struct pkg_node *pkg_node = *(struct pkg_node **)bds_vector_get(pkg_graph->meta_pkgs, i);
                pkg_node_clear_markers(pkg_node);
        }
}

static int __load_sbo(pkg_nodes_t *pkgs, const char *cur_path)
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

                                struct pkg_node *pkg_node = pkg_node_alloc(dirent->d_name);

                                pkg_init_sbo_dir(&pkg_node->pkg, sbo_dir);
                                pkg_set_info_crc(&pkg_node->pkg);
                                pkg_nodes_append(pkgs, pkg_node);
                        }
                }
        }

finish:
        if (dp)
                closedir(dp);
        --level;

        return rc;
}

int pkg_load_sbo(pkg_nodes_t *pkgs)
{
        int rc = 0;
        if ((rc = __load_sbo(pkgs, user_config.sbopkg_repo)) != 0) {
                return rc;
        }
        bds_vector_qsort(pkgs, pkg_nodes_compar);

        return 0;
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

static int __create_db(const char *db_file, pkg_nodes_t *pkgs, bool write_sbo_dir)
{
        FILE *fp = fopen(db_file, "w");
        assert(fp);

        for (size_t i = 0; i < bds_vector_size(pkgs); ++i) {
                struct pkg_node *pkg_node = *(struct pkg_node **)bds_vector_get(pkgs, i);

                if (pkg_node->pkg.name == NULL) /* Package has been removed */
                        continue;

                fprintf(fp, "%s:0x%x", pkg_node->pkg.name, pkg_node->pkg.info_crc);
                if (write_sbo_dir)
                        fprintf(fp, ":%s", pkg_node->pkg.sbo_dir + strlen(user_config.sbopkg_repo) + 1);
                fprintf(fp, "\n");
        }
        fclose(fp);

        return 0;
}

int pkg_create_db(pkg_nodes_t *pkgs)
{
        char db_file[4096];
        bds_string_copyf(db_file, sizeof(db_file), "%s/PKGDB", user_config.depdir);

        return __create_db(db_file, pkgs, true);
}

int pkg_create_reviewed(pkg_nodes_t *pkgs)
{
        char db_file[4096];
        bds_string_copyf(db_file, sizeof(db_file), "%s/REVIEWED", user_config.depdir);

        return __create_db(db_file, pkgs, false);
}

int pkg_create_default_deps(pkg_nodes_t *pkgs)
{
        for (size_t i = 0; i < bds_vector_size(pkgs); ++i) {
                struct pkg_node *pkg_node = *(struct pkg_node **)bds_vector_get(pkgs, i);

                if (dep_file_exists(pkg_node->pkg.name))
                        continue;

                create_default_dep(&pkg_node->pkg);
        }
        return 0;
}

static int __load_db(pkg_nodes_t *pkgs, const char *db_file, bool read_sbo_dir)
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

                struct pkg_node *pkg_node = pkg_node_alloc(tok[0]);

                pkg_node->pkg.info_crc = strtol(tok[1], NULL, 16);

                if (read_sbo_dir) {
                        bds_string_copyf(sbo_dir, sizeof(sbo_dir), "%s/%s", user_config.sbopkg_repo, tok[2]);
                        pkg_init_sbo_dir(&pkg_node->pkg, sbo_dir);
                }
                pkg_nodes_append(pkgs, pkg_node);

                free(tok);
        }
        fclose(fp);

        return 0;
}

int pkg_load_db(pkg_nodes_t *pkgs)
{
        if (!pkg_db_exists())
                return 1;

        char db_file[4096];
        bds_string_copyf(db_file, sizeof(db_file), "%s/PKGDB", user_config.depdir);

        return __load_db(pkgs, db_file, true);
}

int pkg_load_reviewed(pkg_nodes_t *pkgs)
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

int pkg_load_all_deps(struct pkg_graph *pkg_graph, struct pkg_options options)
{
        pkg_nodes_t *pkgs[2] = {pkg_graph->sbo_pkgs, pkg_graph->meta_pkgs};

        for (size_t n = 0; n < 2; ++n) {
                // We load deps for all packages
                for (size_t i = 0; i < bds_vector_size(pkgs[n]); ++i) {
                        struct pkg_node *pkg_node = *(struct pkg_node **)bds_vector_get(pkgs[n], i);

                        if (pkg_node->pkg.name == NULL)
                                continue;

                        int rc = 0;
                        if ((rc = load_dep_file(pkg_graph, pkg_node->pkg.name, options)) != 0)
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

static int next_node_dist(pkg_iterator_flags_t iter_flags, const struct pkg_node *next_node,
                          const struct pkg_node *cur_node)
{
        return !(iter_flags & PKG_ITER_REVDEPS ? next_node->pkg.dep.is_meta : cur_node->pkg.dep.is_meta);
}

static pkg_nodes_t *iterator_edge_nodes(const struct pkg_iterator *iter)
{
        struct pkg_node *cur_node = iter->cur_node;
        if (cur_node == NULL)
                return NULL;

        return (iter->flags & PKG_ITER_REVDEPS ? cur_node->pkg.dep.parents : cur_node->pkg.dep.required);
}

struct pkg_node *pkg_iterator_begin(struct pkg_iterator *iter, struct pkg_graph *pkg_graph, const char *pkg_name,
                                    pkg_iterator_flags_t flags, int max_dist)
{
        memset(iter, 0, sizeof(*iter));

        iter->flags     = flags;
        iter->max_dist = max_dist;
        iter->cur_node = pkg_graph_search(pkg_graph, pkg_name);

        if (iter->cur_node == NULL)
                return NULL;

        pkg_graph_clear_markers(pkg_graph);
        iter->cur_node->color = COLOR_GREY;

        iter->visit_nodes = bds_stack_alloc(1, sizeof(struct pkg_node *), NULL);

        return pkg_iterator_next(iter);
}

struct pkg_node *pkg_iterator_next(struct pkg_iterator *iter)
{

        struct pkg_node *cur_node = iter->cur_node;

        if (cur_node == NULL)
                return NULL;

        pkg_nodes_t *edge_nodes = iterator_edge_nodes(iter);

        const bool at_max_dist = (iter->max_dist >= 0 && cur_node->dist == iter->max_dist);
        const bool no_edge_nodes =
            (edge_nodes ? (cur_node->edge_index < 0 || cur_node->edge_index == bds_vector_size(edge_nodes))
                        : true);

        if (no_edge_nodes || at_max_dist) {
                /*
                  In the case that we are at_max_dist, update the edge index to ensure no more edge nodes are
                  visited for the current node.

                  This shouldn't be needed since, the node will be marked COLOR_BLACK.
                 */
                cur_node->edge_index = -1;

                if ((iter->flags & PKG_ITER_REQ_NEAREST) == 0 )
                        assert(cur_node->color == COLOR_GREY);
                cur_node->color = COLOR_BLACK;

                struct pkg_node *leaf_node = cur_node;

                /* Traverse backwards for the "previous node" */
                iter->cur_node = NULL;
                bds_stack_pop(iter->visit_nodes, &iter->cur_node);

                return leaf_node;
        }

        /*
          If visiting the edge nodes have been disabled we shouldn't reach here. Adding an assertion to enforce
          this logic.
         */
        assert(cur_node->edge_index >= 0);
        while (cur_node->edge_index < bds_vector_size(edge_nodes)) {

                struct pkg_node *next_node =
                    *(struct pkg_node **)bds_vector_get(edge_nodes, cur_node->edge_index++);

                bool visit_next = false;
                if (next_node->color == COLOR_WHITE) {
                        visit_next        = true;
                        next_node->color = COLOR_GREY;
                } else if ((iter->flags & PKG_ITER_REQ_NEAREST) && cur_node->color != COLOR_BLACK) {
                        visit_next = true;
			/* Do not set color */
                }

                if (visit_next) {
                        bds_stack_push(iter->visit_nodes, &cur_node);
                        /*
                          NOTE: We evaluate the next node distance here, but do not terminate based on the
                          iterator's maximum distance. This is done on entry as the distance of the current node is
                          used to determine if we should try to visit the edge nodes.
                         */
                        next_node->dist = cur_node->dist + next_node_dist(iter->flags, next_node, cur_node);
                        iter->cur_node  = next_node;
                        break;
                }
        }

        return pkg_iterator_next(iter);
}

struct pkg_node *pkg_iterator_node(struct pkg_iterator *iter) { return iter->cur_node; }

void pkg_iterator_destroy(struct pkg_iterator *iter)
{
        if (iter->visit_nodes)
                bds_stack_free(&iter->visit_nodes);

        memset(iter, 0, sizeof(*iter));
}
