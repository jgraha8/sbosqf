#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libbds/bds_string.h>
#include <libbds/bds_vector.h>

#include "config.h"
#include "deps.h"
#include "filesystem.h"
#include "input.h"
#include "msg_string.h"
#include "pkg_db.h"
#include "user_config.h"

__attribute__((unused)) static int compar_string_list(const void *a, const void *b)
{
        return strcmp(*((const char **)a), *((const char **)b));
}

static int compar_dep_info(const void *a, const void *b)
{
        return strcmp(((const struct dep_info *)a)->pkg_name, ((const struct dep_info *)b)->pkg_name);
}

static int compar_dep_info_list(const void *a, const void *b)
{
        return strcmp((*(const struct dep_info **)a)->pkg_name, (*(const struct dep_info **)b)->pkg_name);
}

static void free_string_list(char **str)
{
        free(*str);
        *str = NULL;
}

struct dep_info dep_info_ctor(const char *pkg_name)
{
        struct dep_info dep_info;
        memset(&dep_info, 0, sizeof(dep_info));

        if (pkg_name) {
                dep_info.pkg_name = bds_string_dup(pkg_name);
        }

        return dep_info;
}

void dep_info_dtor(struct dep_info *dep_info)
{
        if (dep_info->pkg_name)
                free((char *)dep_info->pkg_name);

        if (dep_info->buildopts)
                bds_vector_free(&dep_info->buildopts);

        memset(dep_info, 0, sizeof(*dep_info));
}

void dep_info_buildopts_init(struct dep_info *dep_info)
{
        dep_info->buildopts = bds_vector_alloc(1, sizeof(char *), (void (*)(void *))free_string_list);
}

struct dep_info dep_info_deep_copy(const struct dep_info *src)
{
        struct dep_info dep_info = dep_info_ctor(src->pkg_name);

        dep_info.is_meta = src->is_meta;

        if (!src->buildopts)
                goto finish;

        const char **bopts_iter = (const char **)bds_vector_ptr(src->buildopts);
        const char **bopts_end  = bopts_iter + bds_vector_size(src->buildopts);

        if (bopts_iter == bopts_end)
                goto finish;

        dep_info_buildopts_init(&dep_info);

        for (; bopts_iter != bopts_end; ++bopts_iter) {
                char *bopt = bds_string_dup(*bopts_iter);
                bds_vector_append(dep_info.buildopts, &bopt);
        }

finish:
        return dep_info;
}

struct dep *dep_alloc(const char *pkg_name)
{
        struct dep *dep = calloc(1, sizeof(*dep));

        *(struct dep_info *)dep = dep_info_ctor(pkg_name);

        return dep;
}

void dep_free(struct dep **dep)
{
        if ((*dep) == NULL)
                return;

        dep_info_dtor((struct dep_info *)(*dep));

        if ((*dep)->required)
                bds_vector_free(&(*dep)->required);
        if ((*dep)->optional)
                bds_vector_free(&(*dep)->optional);

        free(*dep);
        *dep = NULL;
}

void dep_required_init(struct dep *dep)
{
        dep->required = bds_vector_alloc(1, sizeof(struct dep *), (void (*)(void *))dep_free);
}

void dep_optional_init(struct dep *dep)
{
        dep->optional = bds_vector_alloc(1, sizeof(struct dep *), (void (*)(void *))dep_free);
}

struct dep_list *dep_list_alloc(const char *pkg_name)
{
        struct dep_list *dep_list = calloc(1, sizeof(*dep_list));

        *(struct dep_info *)dep_list = dep_info_ctor(pkg_name);
        dep_list->dep_list = bds_vector_alloc(1, sizeof(struct dep_info), (void (*)(void *))dep_info_dtor);

        return dep_list;
}

struct dep_list *dep_list_alloc_with_info(const struct dep_info *info)
{
        struct dep_list *dep_list = calloc(1, sizeof(*dep_list));

        *(struct dep_info *)dep_list = dep_info_deep_copy(info);
        dep_list->dep_list = bds_vector_alloc(1, sizeof(struct dep_info), (void (*)(void *))dep_info_dtor);

        return dep_list;
}

void dep_list_free(struct dep_list **dep_list)
{
        if (*dep_list == NULL)
                return;

        dep_info_dtor((struct dep_info *)(*dep_list));
        bds_vector_free(&(*dep_list)->dep_list);
        free(*dep_list);
        *dep_list = NULL;
}

struct dep *load_dep_file(const char *pkg_name, struct process_options options)
{
        static int level = 0;

        ++level;

        struct dep *dep = NULL;
        char *line      = NULL;
        size_t num_line = 0;
        char *dep_file  = bds_string_dup_concat(3, user_config.depdir, "/", pkg_name);
        FILE *fp        = fopen(dep_file, "r");

        if (fp == NULL) {
                // TODO: if dep file doesn't exist request dependency file add and package addition
                if (display_dep_menu(pkg_name, msg_dep_file_not_found(pkg_name), 0) != 0) {
                        goto finish;
                }
                fp = fopen(dep_file, "r");
                if (fp == NULL) {
                        fprintf(stderr, "unable to open dep file %s\n", dep_file);
                        goto finish;
                }
        }
        dep = dep_alloc(pkg_name);

        enum block_type block_type = NO_BLOCK;

        while (getline(&line, &num_line, fp) != -1) {
                assert(line);

                // Trim newline
                char *c = line;
                while (*c) {
                        if (*c == '\n' || *c == '\t') {
                                *c = ' ';
                        }
                        ++c;
                }

                // Trim comments
                c = bds_string_find(line, "#");
                if (c) {
                        *c = '\0';
                }

                if (*bds_string_atrim(line) == '\0') {
                        goto cycle;
                }

                if (*line == '-') {
                        goto cycle;
                }

                if (strcmp(line, "METAPKG") == 0) {
                        dep->info.is_meta = true;
                        goto cycle;
                }

                if (strcmp(line, "REQUIRED:") == 0) {
                        block_type = REQUIRED_BLOCK;
                        goto cycle;
                }

                if (strcmp(line, "OPTIONAL:") == 0) {
                        block_type = OPTIONAL_BLOCK;
                        goto cycle;
                }

                if (strcmp(line, "BUILDOPTS:") == 0) {
                        block_type = BUILDOPTS_BLOCK;
                        goto cycle;
                }

                if (block_type == OPTIONAL_BLOCK && !options.optional)
                        goto cycle;

                /*
                 * Recursive processing will occur on meta packages since they act as "include" files.  We only
                 * check the recursive flag if the dependency file is not marked as a meta package.
                 */
                if (!dep->info.is_meta && !options.recursive && level > 1)
                        goto finish;

                switch (block_type) {
                case REQUIRED_BLOCK: {
                        if (skip_installed(line, options))
                                break;

                        struct dep *req_dep = load_dep_file(line, options);
                        if (req_dep == NULL) {
                                fprintf(stderr, "%s required dependency file %s not found\n", pkg_name, line);
                                exit(EXIT_FAILURE);
                        }
                        if (dep->required == NULL) {
                                dep_required_init(dep);
                        }
                        bds_vector_append(dep->required, &req_dep);
                } break;
                case OPTIONAL_BLOCK: {
                        if (skip_installed(line, options))
                                break;

                        struct dep *opt_dep = load_dep_file(line, options);
                        if (opt_dep == NULL) {
                                fprintf(stderr, "%s optional dependency file %s not found\n", pkg_name, line);
                                exit(EXIT_FAILURE);
                        }
                        if (dep->optional == NULL) {
                                dep_optional_init(dep);
                        }
                        bds_vector_append(dep->optional, &opt_dep);
                } break;
                case BUILDOPTS_BLOCK: {
                        char *buildopt = bds_string_dup(bds_string_atrim(line));
                        if (dep->info.buildopts == NULL) {
                                dep_info_buildopts_init(&dep->info);
                        }
                        bds_vector_append(dep->info.buildopts, &buildopt);
                } break;
                default:
                        fprintf(stderr, "%s(%d): badly formatted dependency file %s\n", __FILE__, __LINE__,
                                dep_file);
                        exit(EXIT_FAILURE);
                }

        cycle:
                free(line);
                line     = NULL;
                num_line = 0;
        }

finish:
        if (line != NULL) {
                free(line);
        }

        if (fp)
                fclose(fp);
        free(dep_file);
        --level;

        return dep;
}

void __write_dep_sqf(FILE *fp, const struct dep_info *dep_info)
{
        if (dep_info->is_meta)
                return;

        fprintf(fp, "%s", dep_info->pkg_name);

        if (dep_info->buildopts) {
                const char **bopts     = (const char **)bds_vector_ptr(dep_info->buildopts);
                const char **bopts_end = bopts + bds_vector_size(dep_info->buildopts);

                while (bopts != bopts_end) {
                        fprintf(fp, " %s", *bopts);
                        ++bopts;
                }
        }
        fprintf(fp, "\n");
}

void write_dep_sqf(FILE *fp, const struct dep *dep, struct process_options options)
{
        struct dep_list *dep_list = create_dep_list(dep);

        const struct dep_info *di_iter = (const struct dep_info *)bds_vector_ptr(dep_list->dep_list);
        const struct dep_info *di_end  = di_iter + bds_vector_size(dep_list->dep_list);

        if (options.revdeps) {
                __write_dep_sqf(fp, &dep_list->info);
        }

        for (; di_iter != di_end; ++di_iter) {
                __write_dep_sqf(fp, di_iter);
        }

        if (!options.revdeps) {
                __write_dep_sqf(fp, &dep_list->info);
        }
        dep_list_free(&dep_list);
}

void write_sqf(FILE *fp, const struct dep_list *dep_list, struct process_options options)
{
        const struct dep_info *di_iter = (const struct dep_info *)bds_vector_ptr(dep_list->dep_list);
        const struct dep_info *di_end  = di_iter + bds_vector_size(dep_list->dep_list);

        if (options.revdeps) {
                __write_dep_sqf(fp, &dep_list->info);
        }
        for (; di_iter != di_end; ++di_iter) {
                __write_dep_sqf(fp, di_iter);
        }

        if (!options.revdeps) {
                __write_dep_sqf(fp, &dep_list->info);
        }
}

dep_info_vector_t *get_remove_list(const struct dep_list *dep_list, struct process_options options)
{
        dep_info_vector_t *remove_list = dep_info_vector_alloc();

        struct dep_info parent_info = dep_info_ctor(NULL);

        // First see if we can remove the
        if (has_parent_installed(dep_list->info.pkg_name, options, NULL, &parent_info)) {
                fprintf(stderr, "[-] %s required by at least %s\n", dep_list->info.pkg_name, parent_info.pkg_name);
                dep_info_dtor(&parent_info);
                return remove_list;
        }
        dep_info_dtor(&parent_info);

        fprintf(stderr, "[+] %s\n", dep_list->info.pkg_name);
        dep_info_vector_append(remove_list, &dep_list->info);

        // Create ignored list which contains the package we are tring to remove and any of its dependencies
        dep_info_vector_t *ignored_list = dep_info_vector_alloc();
        dep_info_vector_append(ignored_list, &dep_list->info);

        const struct dep_info *di_iter = (const struct dep_info *)bds_vector_ptr(dep_list->dep_list);
        const struct dep_info *di_end  = di_iter + bds_vector_size(dep_list->dep_list);
        for (; di_iter != di_end; ++di_iter) {
                dep_info_vector_append(ignored_list, di_iter);
        }

        di_iter = (const struct dep_info *)bds_vector_ptr(dep_list->dep_list);
        for (; di_iter != di_end; ++di_iter) {
                if (has_parent_installed(di_iter->pkg_name, options, ignored_list, &parent_info)) {
                        fprintf(stderr, "[-] %s required by at least %s\n", di_iter->pkg_name,
                                parent_info.pkg_name);
                } else {
                        fprintf(stderr, "[+] %s\n", di_iter->pkg_name);
                        dep_info_vector_append(remove_list, di_iter);
                }
                dep_info_dtor(&parent_info);
        }
        dep_info_vector_free(&ignored_list);

        return remove_list;
}

static void __write_remove_list(FILE *fp, const struct dep_list *dep_list, struct process_options options,
                                bool output_inline)
{
        dep_info_vector_t *remove_list = get_remove_list(dep_list, options);

        const struct dep_info *di_iter = (const struct dep_info *)bds_vector_ptr(remove_list);
        const struct dep_info *di_end  = di_iter + bds_vector_size(remove_list);

        bool need_newline = false;
        bool need_space   = false;
        for (; di_iter != di_end; ++di_iter) {
                if (output_inline) {
                        if (need_space)
                                fprintf(fp, " ");
                        need_newline = true;
                        need_space   = true;
                }
                fprintf(fp, "%s", di_iter->pkg_name);
                if (!output_inline) {
                        fprintf(fp, "\n");
                }
        }
        if (need_newline)
                fprintf(fp, "\n");

        dep_info_vector_free(&remove_list);
}

void write_remove_sqf(FILE *fp, const struct dep_list *dep_list, struct process_options options)
{
        __write_remove_list(fp, dep_list, options, false);
}

void write_remove_list(FILE *fp, const struct dep_list *dep_list, struct process_options options)
{
        __write_remove_list(fp, dep_list, options, true);
}

void __append_deps(const dep_vector_t *deps, dep_info_vector_t *dep_info_vector)
{
        if (!deps)
                return;

        const struct dep **d_iter = (const struct dep **)bds_vector_ptr(deps);
        const struct dep **d_end  = d_iter + bds_vector_size(deps);

        for (; d_iter != d_end; ++d_iter) {
                __append_deps((*d_iter)->required, dep_info_vector);
                __append_deps((*d_iter)->optional, dep_info_vector);

                const struct dep_info key = {.pkg_name = (*d_iter)->info.pkg_name};

                if (bds_vector_lsearch(dep_info_vector, &key, compar_dep_info) == NULL) {

                        struct dep_info dep_info = dep_info_deep_copy((struct dep_info *)(*d_iter));
                        bds_vector_append(dep_info_vector, &dep_info);
                }
        }
}

struct dep_list *load_dep_list(const char *pkg_name, struct process_options options)
{
        struct dep *dep = load_dep_file(pkg_name, options);
        if (!dep)
                return NULL;

        struct dep_list *dep_list = create_dep_list(dep);

        dep_free(&dep);

        return dep_list;
}

struct dep_list *create_dep_list(const struct dep *dep)
{
        struct dep_list *dep_list = dep_list_alloc_with_info((struct dep_info *)dep);

        __append_deps(dep->required, dep_list->dep_list);
        __append_deps(dep->optional, dep_list->dep_list);

        return dep_list;
}

void write_dep_list(FILE *fp, const struct dep_list *dep_list)
{
        fprintf(fp, "%s:", dep_list->info.pkg_name);

        const struct dep_info *di_iter = (const struct dep_info *)bds_vector_ptr(dep_list->dep_list);
        const struct dep_info *di_end  = di_iter + bds_vector_size(dep_list->dep_list);

        while (di_iter != di_end) {
                fprintf(fp, " %s", di_iter->pkg_name);

                ++di_iter;
        }
        fprintf(fp, "\n");
}

int write_depdb(struct process_options options)
{
        int rc = 0;

        char tmpdb[4096];
        char depdb[4096];

        bds_string_copyf(tmpdb, sizeof(tmpdb), "%s/.%s", user_config.depdir, DEPDB);
        bds_string_copyf(depdb, sizeof(depdb), "%s/%s", user_config.depdir, DEPDB);

        FILE *fp = fopen(tmpdb, "w");
        if (fp == NULL) {
                rc = 1;
                goto finish;
        }
        struct bds_list_node *node = NULL;

        for (node = bds_list_begin(pkg_db_pkglist); node != NULL; node = bds_list_iterate(node)) {
                const struct pkg *pkg     = (const struct pkg *)bds_list_object(node);
                struct dep_list *dep_list = NULL;

                while (1) {
                        if (find_dep_file(pkg->name) == NULL) {
                                display_dep_menu(pkg->name, msg_dep_file_not_found(pkg->name), 0);
                                continue;
                        }

                        if (find_pkg(pkg_db_reviewed, pkg->name) == NULL) {
                                display_dep_menu(pkg->name, msg_pkg_not_reviewed(pkg->name), MENU_DELETE_DEP);
                                continue;
                        }

                        dep_list = load_dep_list(pkg->name, options);
                        break;
                }

                if (dep_list == NULL) {
                        rc = 1;
                        goto finish;
                }

                write_dep_list(fp, dep_list);
                dep_list_free(&dep_list);
        }

finish:
        if (fp)
                fclose(fp);

        if (rc == 0) {
                if (rename(tmpdb, depdb) != 0) {
                        perror("rename()");
                        rc = 1;
                }
        } else {
                if (unlink(tmpdb) == -1)
                        perror("unlink()");
        }

        return rc;
}

struct dep_parents *dep_parents_alloc(const char *pkg_name)
{
        struct dep_parents *dp = calloc(1, sizeof(*dp));

        *(struct dep_info *)dp = dep_info_ctor(pkg_name);
        dp->parents_list       = bds_vector_alloc(1, sizeof(struct dep_info), (void (*)(void *))dep_info_dtor);

        return dp;
}

struct dep_parents *dep_parents_alloc2(const struct dep_info *info)
{
        struct dep_parents *dp = calloc(1, sizeof(*dp));

        dp->info         = dep_info_deep_copy(info);
        dp->parents_list = bds_vector_alloc(1, sizeof(struct dep_info), (void (*)(void *))dep_info_dtor);

        return dp;
}

void dep_parents_free(struct dep_parents **dp)
{
        if (*dp == NULL)
                return;

        dep_info_dtor(&(*dp)->info);
        if ((*dp)->parents_list) {
                bds_vector_free(&(*dp)->parents_list);
        }
        free(*dp);
        *dp = NULL;
}

dep_info_vector_t *dep_info_vector_alloc()
{
        dep_info_vector_t *div = bds_vector_alloc(1, sizeof(struct dep_info), (void (*)(void *))dep_info_dtor);
        return div;
}

void dep_info_vector_free(dep_info_vector_t **div)
{
        if (*div == NULL)
                return;

        bds_vector_free(div);
}

void dep_info_vector_append(dep_info_vector_t *div, const struct dep_info *dep_info)
{
        struct dep_info di = dep_info_deep_copy(dep_info);
        bds_vector_append(div, &di);
}

struct dep_info *dep_info_vector_search(dep_info_vector_t *div, const char *pkg_name)
{
        struct dep_info key = {.pkg_name = pkg_name};
        return (struct dep_info *)bds_vector_lsearch(div, &key, compar_dep_info);
}

struct dep_info *dep_info_vector_begin(dep_info_vector_t *div) { return (struct dep_info *)bds_vector_ptr(div); }

struct dep_info *dep_info_vector_end(dep_info_vector_t *div)
{
        return dep_info_vector_begin(div) + bds_vector_size(div);
}

dep_parents_vector_t *dep_parents_vector_alloc()
{
        dep_parents_vector_t *dp_vector =
            bds_vector_alloc(1, sizeof(struct dep_parents *), (void (*)(void *))dep_parents_free);
        return dp_vector;
}
void dep_parents_vector_free(dep_parents_vector_t **dp_vector)
{
        if (*dp_vector == NULL)
                return;
        bds_vector_free(dp_vector);
}

struct dep_parents *dep_parents_vector_search(dep_parents_vector_t *dp_vector, const char *pkg_name)
{
        struct dep_parents *key = dep_parents_alloc(pkg_name);
        struct dep_parents **dp = (struct dep_parents **)bds_vector_lsearch(dp_vector, &key, compar_dep_info_list);

        dep_parents_free(&key);

        return *dp;
}

struct dep_parents *load_dep_parents(const char *pkg_name, struct process_options options, bool include_pkg)
{
        struct dep *dep           = load_dep_file(pkg_name, options);
        struct dep_list *dep_list = NULL;

        struct dep_parents *dp = dep_parents_alloc2(&dep->info);

        struct dep_info *di_iter, *di_begin;
        struct dep_info *di_end;

        if (dep->info.is_meta) {
                dep_list = create_dep_list(dep);

                di_begin = dep_info_vector_begin(dep_list->dep_list);
                di_end   = dep_info_vector_end(dep_list->dep_list);
        } else {
                di_begin = &dep->info;
                di_end   = di_begin + 1;
        }

	if( include_pkg ) {
		for (di_iter = di_end - 1; di_iter >= di_begin; --di_iter) {
			dep_info_vector_append(dp->parents_list, di_iter);
		}
	}

        struct bds_list_node *node = NULL;

        for (node = bds_list_begin(pkg_db_pkglist); node != NULL; node = bds_list_iterate(node)) {
                const struct pkg *pkg = (const struct pkg *)bds_list_object(node);

                struct dep_list *pkg_dep_list = load_dep_list(pkg->name, options);
                assert(pkg_dep_list);

                for (di_iter = di_begin; di_iter != di_end; ++di_iter) {
                        if (strcmp(pkg->name, di_iter->pkg_name) == 0)
                                continue;

                        // Search the dependency list to see if package is present.
                        if (dep_info_vector_search(pkg_dep_list->dep_list, di_iter->pkg_name) != NULL) {
				if( dep_info_vector_search(dp->parents_list, pkg_dep_list->info.pkg_name) == NULL ) {
					struct dep_info parent = dep_info_deep_copy(&pkg_dep_list->info);
					dep_info_vector_append(dp->parents_list, &parent);
				}
                        }
                }
                dep_list_free(&pkg_dep_list);
        }
        if (dep_list)
                dep_list_free(&dep_list);

        dep_free(&dep);

        return dp;
}

int write_parentdb(struct process_options options)
{
        int rc   = 0;
        FILE *fp = NULL;

        dep_parents_vector_t *dp_vector = dep_parents_vector_alloc();
        struct bds_list_node *node      = NULL;

        for (node = bds_list_begin(pkg_db_pkglist); node != NULL; node = bds_list_iterate(node)) {
                const struct pkg *pkg  = (const struct pkg *)bds_list_object(node);
                struct dep_parents *dp = load_dep_parents(pkg->name, options, false);
                bds_vector_append(dp_vector, &dp);
        }

        char tmpdb[4096];
        char depdb[4096];

        bds_string_copyf(tmpdb, sizeof(tmpdb), "%s/.%s", user_config.depdir, PARENTDB);
        bds_string_copyf(depdb, sizeof(depdb), "%s/%s", user_config.depdir, PARENTDB);

        if ((fp = fopen(tmpdb, "w")) == NULL) {
                rc = 1;
                goto finish;
        }

        const struct dep_parents **dp_begin = (const struct dep_parents **)bds_vector_ptr(dp_vector);
        const struct dep_parents **dp_end   = dp_begin + bds_vector_size(dp_vector);
        const struct dep_parents **dp_iter  = NULL;

        for (dp_iter = dp_begin; dp_iter != dp_end; ++dp_iter) {
                fprintf(fp, "%s:", (*dp_iter)->info.pkg_name);

                const struct dep_info *di_begin =
                    (const struct dep_info *)bds_vector_ptr((*dp_iter)->parents_list);
                const struct dep_info *di_end  = di_begin + bds_vector_size((*dp_iter)->parents_list);
                const struct dep_info *di_iter = NULL;

                for (di_iter = di_begin; di_iter != di_end; ++di_iter) {
                        fprintf(fp, " %s", di_iter->pkg_name);
                }
                fprintf(fp, "\n");
        }

finish:
        if (fp)
                fclose(fp);
        if (dp_vector)
                dep_parents_vector_free(&dp_vector);

        if (rc == 0) {
                if (rename(tmpdb, depdb) != 0) {
                        perror("rename()");
                        rc = 1;
                }
        } else {
                if (unlink(tmpdb) == -1)
                        perror("unlink()");
        }

        return rc;
}

const char *create_default_dep_file(const char *pkg_name)
{
        static char dep_file[4096];

        FILE *fp            = NULL;
        char **required     = NULL;
        size_t num_required = 0;

        const char *rp = NULL;

        // First check if dep file already exists
        if (find_dep_file(pkg_name)) {
                return NULL;
        }

        // Tokenize sbo_requires
        const char *sbo_dir = find_sbo_dir(user_config.sbopkg_repo, pkg_name);
        if (!sbo_dir) {
                goto finish;
        }

        const char *sbo_requires = read_sbo_requires(sbo_dir, pkg_name);
        if (!sbo_requires) {
                goto finish;
        }

        bds_string_copyf(dep_file, sizeof(dep_file), "%s/%s", user_config.depdir, pkg_name);

        fp = fopen(dep_file, "w");
        if (fp == NULL) {
                perror("fopen");
                goto finish;
        }
        fprintf(fp, "REQUIRED:\n");

        bds_string_tokenize((char *)sbo_requires, " ", &num_required, &required);
        for (size_t i = 0; i < num_required; ++i) {
                if (required[i] == NULL)
                        continue;
                if (bds_string_atrim(required[i]) == 0)
                        continue;
                if (strcmp(required[i], "%README%") == 0)
                        continue;

                fprintf(fp, "%s\n", required[i]);
        }

        fprintf(fp, "\nOPTIONAL:\n");
        fprintf(fp, "\nBUILDOPTS:\n");

        rp = dep_file;
finish:
        if (fp)
                fclose(fp);
        if (required)
                free(required);

        return rp;
}

int perform_dep_action(const char *pkg_name, int action)
{
        switch (action) {
        case MENU_CREATE_DEP:
                if (create_default_dep_file(pkg_name) == NULL) {
                        fprintf(stderr, "unable to create default dependency file for package %s\n", pkg_name);
                        return 1;
                }
                return perform_dep_action(pkg_name, MENU_ADD_PKG);
        case MENU_ADD_PKG:
                if (add_pkg(pkg_db_pkglist, PKGLIST, pkg_name) != 0) {
                        fprintf(stderr, "unable to add %s to PKGLIST\n", pkg_name);
                        return 1;
                }
                break;
        case MENU_REVIEW_PKG: {
		struct process_options po;
		memset(&po, 0, sizeof(po));
		po.optional = true;

		struct dep *dep = load_dep_file(pkg_name, po);
		assert(dep);
		
		if( dep->info.is_meta ) {
			struct dep_list *dep_list = create_dep_list(dep);
			dep_free(&dep);
			
			for( struct dep_info *di_iter = dep_info_vector_begin(dep_list->dep_list);
			     di_iter != dep_info_vector_end(dep_list->dep_list);
			     ++di_iter ) {
				if (review_pkg(di_iter->pkg_name) != 0) {
					fprintf(stderr, "unable to review package %s\n", di_iter->pkg_name);
					dep_list_free(&dep_list);
					return 1;					
				}
				int rc;
				if( (rc = perform_dep_action(di_iter->pkg_name, MENU_ADD_REVIEWED)) != 0 ) {
					dep_list_free(&dep_list);
					return 1;
				}
			}
			dep_list_free(&dep_list);
			break;
		}
		
		if (review_pkg(pkg_name) != 0) {
			fprintf(stderr, "unable to review package %s\n", pkg_name);
			dep_free(&dep);
			return 1;
		}

                return perform_dep_action(pkg_name, MENU_ADD_REVIEWED);
	}
        case MENU_EDIT_DEP:
                if (edit_dep_file(pkg_name) != 0) {
                        fprintf(stderr, "unable to edit %s dependency file\n", pkg_name);
                        return 1;
                }
                break;
        case MENU_DELETE_DEP:
                if (delete_dep_file(pkg_name) != 0) {
                        fprintf(stderr, "unable to delete %s dependency file\n", pkg_name);
                        return 1;
                }
                break;
        case MENU_ADD_REVIEWED:
                if (add_pkg(pkg_db_reviewed, REVIEWED, pkg_name) != 0) {
                        fprintf(stderr, "unable to add %s to REVIEWED\n", pkg_name);
                        return 1;
                }
                break;
        case MENU_REMOVE_REVIEWED:
                if (remove_pkg(pkg_db_reviewed, REVIEWED, pkg_name) != 0) {
                        fprintf(stderr, "unable to remove %s from REVIEWED\n", pkg_name);
                        return 1;
                }
                break;
        case MENU_NONE:
                break;
        default:
                fprintf(stderr, "incorrect menu item\n");
                return 1;
        }

        return 0;
}

int display_dep_menu(const char *pkg_name, const char *msg, int disabled_options)
{
        int menu_options;

        char title[4096];

        bds_string_copyf(title, sizeof(title), "+----------------------------------------+\n"
                                               "+ Management Menu for Package %s\n"
                                               "+----------------------------------------+",
                         pkg_name);

        do {
                menu_options         = MENU_NONE;
                const char *dep_file = NULL;

                if ((dep_file = find_dep_file(pkg_name)) == NULL) {
                        menu_options |= MENU_CREATE_DEP;
                } else {
                        menu_options |= MENU_EDIT_DEP | MENU_REVIEW_PKG | MENU_DELETE_DEP;
                }

                if (find_pkg(pkg_db_pkglist, pkg_name)) {
                        if (find_pkg(pkg_db_reviewed, pkg_name)) {
                                menu_options |= MENU_REMOVE_REVIEWED;
                        }
                }
                menu_options ^= (menu_options & disabled_options);

                int action = menu_options = menu_display(menu_options, title, msg);
                perform_dep_action(pkg_name, action);

                msg = NULL; // Only use external message on first pass

        } while (menu_options != MENU_NONE);

        return 0;
}

const char *find_dep_file(const char *pkg_name)
{
        static char dep_file[4096];
        struct stat sb;

        bds_string_copyf(dep_file, sizeof(dep_file), "%s/%s", user_config.depdir, pkg_name);

        if (stat(dep_file, &sb) == 0) {
                if (sb.st_mode & S_IFREG) {
                        return dep_file;
                } else if (sb.st_mode & S_IFDIR) {
                        fprintf(stderr, "dependency file for package %s already exists as directory: %s\n",
                                pkg_name, dep_file);
                } else {
                        fprintf(stderr, "dependency file for package %s already exists as non-standard file: %s\n",
                                pkg_name, dep_file);
                }
        }
        return NULL;
}

int edit_dep_file(const char *pkg_name)
{
        char cmd[4096];

        const char *dep_file = find_dep_file(pkg_name);
        if (!dep_file) {
                return display_dep_menu(pkg_name, msg_dep_file_not_found(pkg_name), 0);
        }

        bds_string_copyf(cmd, sizeof(cmd), "%s %s/%s", user_config.editor, user_config.depdir, pkg_name);
        return system(cmd);
}

int delete_dep_file(const char *pkg_name)
{
        const char *dep_file = find_dep_file(pkg_name);

        if (dep_file) {
                if (unlink(dep_file) == -1)
                        perror("unlink()");
        }

        remove_pkg(pkg_db_pkglist, PKGLIST, pkg_name);
        remove_pkg(pkg_db_reviewed, REVIEWED, pkg_name);

        return 0;
}

bool skip_installed(const char *pkg_name, struct process_options options)
{
        if (options.check_installed) {
                const char *check_tag =
                    (options.check_installed & CHECK_ANY_INSTALLED ? NULL : user_config.sbo_tag);
                if (is_pkg_installed(pkg_name, check_tag)) {
                        return true;
                }
        }
        return false;
}

bool has_parent_installed(const char *pkg_name, struct process_options options, dep_info_vector_t *ignored_list,
                          struct dep_info *parent_info)
{
        if (parent_info)
                dep_info_dtor(parent_info);

        struct dep_list *revdeps = (struct dep_list *)load_dep_parents(pkg_name, options, false);
        if (revdeps == NULL)
                return false;

        const struct dep_info *di_iter = (const struct dep_info *)bds_vector_ptr(revdeps->dep_list);
        const struct dep_info *di_end  = di_iter + bds_vector_size(revdeps->dep_list);

        for (; di_iter != di_end; ++di_iter) {
                if (ignored_list) {
                        if (dep_info_vector_search(ignored_list, di_iter->pkg_name))
                                continue;
                }

                if (is_pkg_installed(di_iter->pkg_name, NULL)) {
                        if (parent_info) {
                                *parent_info = dep_info_deep_copy(di_iter);
                        }
                        return true;
                }
        }

        return false;
}

// 1. Given a package name see if it has not parents installed (has_parent_installed)
// 2. For each dependency, see which can be removed
