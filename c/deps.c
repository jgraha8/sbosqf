#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libbds/bds_stack.h>
#include <libbds/bds_string.h>

#include "config.h"
#include "deps.h"
#include "filesystem.h"
#include "pkglist.h"
#include "response.h"
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

        dep_info.pkg_name = bds_string_dup(pkg_name);

        return dep_info;
}

void dep_info_dtor(struct dep_info *dep_info)
{
        free(dep_info->pkg_name);

        if (dep_info->buildopts)
                bds_stack_free(&dep_info->buildopts);

        memset(dep_info, 0, sizeof(*dep_info));
}

void dep_info_buildopts_init(struct dep_info *dep_info)
{
        dep_info->buildopts = bds_stack_alloc(1, sizeof(char *), (void (*)(void *))free_string_list);
}

struct dep_info dep_info_deep_copy(const struct dep_info *src)
{
        struct dep_info dep_info = dep_info_ctor(src->pkg_name);

        dep_info.is_meta = src->is_meta;

        if (!src->buildopts)
                goto finish;

        const char **bopts_iter = (const char **)bds_stack_ptr(src->buildopts);
        const char **bopts_end  = bopts_iter + bds_stack_size(src->buildopts);

        if (bopts_iter == bopts_end)
                goto finish;

        dep_info_buildopts_init(&dep_info);

        for (; bopts_iter != bopts_end; ++bopts_iter) {
                char *bopt = bds_string_dup(*bopts_iter);
                bds_stack_push(dep_info.buildopts, &bopt);
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
                bds_stack_free(&(*dep)->required);
        if ((*dep)->optional)
                bds_stack_free(&(*dep)->optional);

        free(*dep);
        *dep = NULL;
}

void dep_required_init(struct dep *dep)
{
        dep->required = bds_stack_alloc(1, sizeof(struct dep *), (void (*)(void *))dep_free);
}

void dep_optional_init(struct dep *dep)
{
        dep->optional = bds_stack_alloc(1, sizeof(struct dep *), (void (*)(void *))dep_free);
}

struct dep_list *dep_list_alloc(const char *pkg_name)
{
        struct dep_list *dep_list = calloc(1, sizeof(*dep_list));

        *(struct dep_info *)dep_list = dep_info_ctor(pkg_name);
        dep_list->dep_list = bds_stack_alloc(1, sizeof(struct dep_info), (void (*)(void *))dep_info_dtor);

        return dep_list;
}

struct dep_list *dep_list_alloc_with_info(const struct dep_info *info)
{
        struct dep_list *dep_list = calloc(1, sizeof(*dep_list));

        *(struct dep_info *)dep_list = dep_info_deep_copy(info);
        dep_list->dep_list = bds_stack_alloc(1, sizeof(struct dep_info), (void (*)(void *))dep_info_dtor);

        return dep_list;
}

void dep_list_free(struct dep_list **dep_list)
{
        if (*dep_list == NULL)
                return;

        dep_info_dtor((struct dep_info *)(*dep_list));
        bds_stack_free(&(*dep_list)->dep_list);
        free(*dep_list);
        *dep_list = NULL;
}

struct dep *load_dep_file(const char *pkg_name, bool recursive, bool optional)
{
        static int level = 0;

        ++level;

        struct dep *dep = NULL;
        char *line      = NULL;
        size_t num_line = 0;
        char *dep_file  = bds_string_dup_concat(3, user_config.depdir, "/", pkg_name);
        FILE *fp        = fopen(dep_file, "r");

        if (fp == NULL) {
                printf("dependency file %s not found: ", pkg_name);
                // TODO: if dep file doesn't exist request dependency file add and package addition
                if (request_add_dep_file(pkg_name) != 0) {
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

                if (block_type == OPTIONAL_BLOCK && !optional)
                        goto cycle;

                /*
                 * Recursive processing will occur on meta packages since they act as "include" files.  We only
                 * check the recursive flag if the dependency file is not marked as a meta package.
                 */
                if (!dep->info.is_meta && !recursive && level > 1)
                        goto finish;

                switch (block_type) {
                case REQUIRED_BLOCK: {
                        struct dep *req_dep = load_dep_file(line, recursive, optional);
                        if (req_dep == NULL) {
                                fprintf(stderr, "%s required dependency file %s not found\n", pkg_name, line);
                                exit(EXIT_FAILURE);
                        }
                        if (dep->required == NULL) {
                                dep_required_init(dep);
                        }
                        bds_stack_push(dep->required, &req_dep);
                } break;
                case OPTIONAL_BLOCK: {
                        struct dep *opt_dep = load_dep_file(line, recursive, optional);
                        if (opt_dep == NULL) {
                                fprintf(stderr, "%s optional dependency file %s not found\n", pkg_name, line);
                                exit(EXIT_FAILURE);
                        }
                        if (dep->optional == NULL) {
                                dep_optional_init(dep);
                        }
                        bds_stack_push(dep->optional, &opt_dep);
                } break;
                case BUILDOPTS_BLOCK: {
                        char *buildopt = bds_string_dup(bds_string_atrim(line));
                        if (dep->info.buildopts == NULL) {
                                dep_info_buildopts_init(&dep->info);
                        }
                        bds_stack_push(dep->info.buildopts, &buildopt);
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

void __print_dep_sqf(const struct dep_info *dep_info)
{
        if (dep_info->is_meta)
                return;

        printf("%s", dep_info->pkg_name);

        if (dep_info->buildopts) {
                const char **bopts     = (const char **)bds_stack_ptr(dep_info->buildopts);
                const char **bopts_end = bopts + bds_stack_size(dep_info->buildopts);

                while (bopts != bopts_end) {
                        printf(" %s", *bopts);
                        ++bopts;
                }
        }
        printf("\n");
}

void print_dep_sqf(const struct dep *dep)
{
        struct dep_list *dep_list = load_dep_list_from_dep(dep);

        const struct dep_info *di_iter = (const struct dep_info *)bds_stack_ptr(dep_list->dep_list);
        const struct dep_info *di_end  = di_iter + bds_stack_size(dep_list->dep_list);

        for (; di_iter != di_end; ++di_iter) {
                __print_dep_sqf(di_iter);
        }
        __print_dep_sqf(&dep_list->info);

        dep_list_free(&dep_list);
}

/* void __write_deps_list(FILE *fp, const dep_stack_t *deps) */
/* { */
/*         if (!deps) */
/*                 return; */

/*         const struct dep **d     = (const struct dep **)bds_stack_ptr(deps);
 */
/*         const struct dep **d_end = d + bds_stack_size(deps); */

/*         for (; d != d_end; ++d) { */
/*                 write_deplist(fp, *d); */
/*         } */
/* } */

void __append_deps(const dep_stack_t *deps, dep_info_stack_t *dep_info_stack)
{
        if (!deps)
                return;

        const struct dep **d_iter = (const struct dep **)bds_stack_ptr(deps);
        const struct dep **d_end  = d_iter + bds_stack_size(deps);

        for (; d_iter != d_end; ++d_iter) {
                __append_deps((*d_iter)->required, dep_info_stack);
                __append_deps((*d_iter)->optional, dep_info_stack);

                struct dep_info key = {.pkg_name = (*d_iter)->info.pkg_name};

                if (bds_stack_lsearch(dep_info_stack, &key, compar_dep_info) == NULL) {

                        struct dep_info dep_info = dep_info_deep_copy((struct dep_info *)(*d_iter));
                        bds_stack_push(dep_info_stack, &dep_info);
                }
        }
}

struct dep_list *load_dep_list(const char *pkg_name, bool recursive, bool optional)
{
        struct dep *dep = load_dep_file(pkg_name, recursive, optional);
        if (!dep)
                return NULL;

        struct dep_list *dep_list = load_dep_list_from_dep(dep);

        dep_free(&dep);

        return dep_list;
}

struct dep_list *load_dep_list_from_dep(const struct dep *dep)
{
        struct dep_list *dep_list = dep_list_alloc_with_info((struct dep_info *)dep);

        __append_deps(dep->required, dep_list->dep_list);
        __append_deps(dep->optional, dep_list->dep_list);

        return dep_list;
}

void write_dep_list(FILE *fp, const struct dep_list *dep_list)
{
        fprintf(fp, "%s:", dep_list->info.pkg_name);

        const struct dep_info *di_iter = (const struct dep_info *)bds_stack_ptr(dep_list->dep_list);
        const struct dep_info *di_end  = di_iter + bds_stack_size(dep_list->dep_list);

        while (di_iter != di_end) {
                fprintf(fp, " %s", di_iter->pkg_name);

                ++di_iter;
        }
        fprintf(fp, "\n");
}

int write_depdb(const pkg_stack_t *pkglist, bool recursive, bool optional)
{
        int rc   = 0;
        FILE *fp = fopen(DEPDB, "w");
        if (fp == NULL) {
                rc = 1;
                goto finish;
        }

        const struct pkg *pkg_iter = (const struct pkg *)bds_stack_ptr(pkglist);
        const struct pkg *pkg_end  = pkg_iter + bds_stack_size(pkglist);

        while (pkg_iter != pkg_end) {
                struct dep_list *dep_list = load_dep_list(pkg_iter->name, recursive, optional);
                if (dep_list == NULL) {
                        rc = 2;
                        goto finish;
                }
                write_dep_list(fp, dep_list);
                dep_list_free(&dep_list);

                ++pkg_iter;
        }

finish:
        if (fp)
                fclose(fp);

        return rc;
}

struct dep_parents *dep_parents_alloc(const char *pkg_name)
{
        struct dep_parents *dp = calloc(1, sizeof(*dp));

        *(struct dep_info *)dp = dep_info_ctor(pkg_name);
        dp->parents_list       = bds_stack_alloc(1, sizeof(struct dep_info), (void (*)(void *))dep_info_dtor);

        return dp;
}

void dep_parents_free(struct dep_parents **dp)
{
        if (*dp == NULL)
                return;

        dep_info_dtor((struct dep_info *)(*dp));
        if ((*dp)->parents_list) {
                bds_stack_free(&(*dp)->parents_list);
        }
        free(*dp);
        *dp = NULL;
}

dep_parents_stack_t *dep_parents_stack_alloc()
{
        dep_parents_stack_t *dp_stack =
            bds_stack_alloc(1, sizeof(struct dep_parents *), (void (*)(void *))dep_parents_free);
        return dp_stack;
}
void dep_parents_stack_free(dep_parents_stack_t **dp_stack)
{
        if (*dp_stack == NULL)
                return;
        bds_stack_free(dp_stack);
}

struct dep_parents *dep_parents_stack_search(dep_parents_stack_t *dp_stack, const char *pkg_name)
{
        struct dep_parents *key = dep_parents_alloc(pkg_name);
        struct dep_parents **dp = (struct dep_parents **)bds_stack_lsearch(dp_stack, &key, compar_dep_info_list);

        dep_parents_free(&key);

        return *dp;
}

int write_parentdb(const pkg_stack_t *pkglist, bool recursive, bool optional)
{
        int rc   = 0;
        FILE *fp = fopen(PARENTDB, "w");
        if (fp == NULL) {
                rc = 1;
                goto finish;
        }

        const struct pkg *pkg_begin = (const struct pkg *)bds_stack_ptr(pkglist);
        const struct pkg *pkg_end   = pkg_begin + bds_stack_size(pkglist);
        const struct pkg *pkg_iter  = NULL;

        dep_parents_stack_t *dp_stack = dep_parents_stack_alloc();

        for (pkg_iter = pkg_begin; pkg_iter != pkg_end; ++pkg_iter) {
                struct dep_parents *dp = dep_parents_alloc(pkg_iter->name);
                bds_stack_push(dp_stack, &dp);
        }

        for (pkg_iter = pkg_begin; pkg_iter != pkg_end; ++pkg_iter) {
                struct dep_list *dep_list = load_dep_list(pkg_iter->name, recursive, optional);
                if (dep_list == NULL) {
                        continue;
                        /* rc = 2; */
                        /* goto finish; */
                }

                const struct dep_info *di_begin = (const struct dep_info *)bds_stack_ptr(dep_list->dep_list);
                const struct dep_info *di_end   = di_begin + bds_stack_size(dep_list->dep_list);
                const struct dep_info *di_iter  = NULL;

                for (di_iter = di_begin; di_iter != di_end; ++di_iter) {
                        struct dep_parents *dp = dep_parents_stack_search(dp_stack, di_iter->pkg_name);
                        assert(dp);

                        struct dep_info parent = dep_info_ctor(dep_list->info.pkg_name);
                        bds_stack_push(dp->parents_list, &parent);
                }
                dep_list_free(&dep_list);
        }

        const struct dep_parents **dp_begin = (const struct dep_parents **)bds_stack_ptr(dp_stack);
        const struct dep_parents **dp_end   = dp_begin + bds_stack_size(dp_stack);
        const struct dep_parents **dp_iter  = NULL;

        for (dp_iter = dp_begin; dp_iter != dp_end; ++dp_iter) {
                fprintf(fp, "%s:", (*dp_iter)->info.pkg_name);

                const struct dep_info *di_begin = (const struct dep_info *)bds_stack_ptr((*dp_iter)->parents_list);
                const struct dep_info *di_end   = di_begin + bds_stack_size((*dp_iter)->parents_list);
                const struct dep_info *di_iter  = NULL;

                for (di_iter = di_begin; di_iter != di_end; ++di_iter) {
                        fprintf(fp, " %s", di_iter->pkg_name);
                }
                fprintf(fp, "\n");
        }

finish:
        if (fp)
                fclose(fp);
        if (dp_stack)
                dep_parents_stack_free(&dp_stack);

        return rc;
}

int create_default_dep_file(const char *pkg_name)
{
        int rc              = 0;
        FILE *fp            = NULL;
        char **required     = NULL;
        size_t num_required = 0;

        // First check if dep file already exists
        struct stat sb;

        char *dep_file = bds_string_dup_concat(3, user_config.depdir, "/", pkg_name);

        if (stat(dep_file, &sb) == 0) {
                if (sb.st_mode & S_IFREG) {
                        fprintf(stderr, "dependency file for package %s already exists: %s\n", pkg_name, dep_file);
                        rc = 1;
                        goto finish;
                } else if (sb.st_mode & S_IFDIR) {
                        fprintf(stderr, "dependency file for package %s already exists as directory: %s\n",
                                pkg_name, dep_file);
                        rc = 2;
                        goto finish;
                } else {
                        fprintf(stderr, "dependency file for package %s already exists as non-standard file: %s\n",
                                pkg_name, dep_file);
                        rc = 3;
                        goto finish;
                }
        }

        // Tokenize sbo_requires
        const char *sbo_dir = find_sbo_dir(user_config.sbopkg_repo, pkg_name);
        if (!sbo_dir) {
                rc = 4;
                goto finish;
        }

        const char *sbo_requires = read_sbo_requires(sbo_dir, pkg_name);
        if (!sbo_requires) {
                rc = 5;
                goto finish;
        }

        fp = fopen(dep_file, "w");
        if (fp == NULL) {
                perror("fopen");
                rc = 6;
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

finish:
        if (dep_file)
                free(dep_file);
        if (fp)
                fclose(fp);
        if (required)
                free(required);

        return rc;
}

int request_add_dep_file(const char *pkg_name)
{
        printf("Add dependency file %s (y/n)? ", pkg_name);
        fflush(stdout);

        if (read_response() != 'y') {
                return 1;
        }

        if (create_default_dep_file(pkg_name) != 0) {
                fprintf(stderr, "unable to add dependency file %s\n", pkg_name);
        }
        return 0;
}
