#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libbds/bds_stack.h>
#include <libbds/bds_string.h>

#include "config.h"
#include "depfile.h"
#include "pkglist.h"

struct dep *dep_alloc(const char *pkg_name)
{
        struct dep *dep = calloc(1, sizeof(*dep));
        dep->pkg_name   = bds_string_dup(pkg_name);

        return dep;
}

void dep_free(struct dep **dep)
{
        if ((*dep) == NULL)
                return;

        free((*dep)->pkg_name);
        if ((*dep)->required)
                bds_stack_free(&(*dep)->required);
        if ((*dep)->optional)
                bds_stack_free(&(*dep)->optional);
        if ((*dep)->buildopts)
                bds_stack_free(&(*dep)->buildopts);

        free(*dep);
        *dep = NULL;
}

static void free_string(char **str)
{
        free(*str);
        *str = NULL;
}

struct dep *load_depfile(const char *depdir, const char *pkg_name, bool recursive)
{
        static int level = 0;

        ++level;

        struct dep *dep = NULL;
        char *dep_file  = bds_string_dup_concat(3, depdir, "/", pkg_name);
        FILE *fp        = fopen(dep_file, "r");

        if (fp == NULL) {
                goto finish;
        }
        dep = dep_alloc(pkg_name);

        if (!recursive && level > 1)
                goto finish;

        char *line      = NULL;
        size_t num_line = 0;

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
                        dep->is_meta = true;
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

                switch (block_type) {
                case REQUIRED_BLOCK: {
                        struct dep *req_dep = load_depfile(depdir, line, recursive);
                        if (req_dep == NULL) {
                                fprintf(stderr, "%s required dependency file %s not found\n", pkg_name, line);
                                break;
                        }
                        if (dep->required == NULL) {
                                dep->required =
                                    bds_stack_alloc(1, sizeof(struct dep *), (void (*)(void *))dep_free);
                        }
                        bds_stack_push(dep->required, &req_dep);
                } break;
                case OPTIONAL_BLOCK: {
                        struct dep *opt_dep = load_depfile(depdir, line, recursive);
                        if (opt_dep == NULL) {
                                fprintf(stderr, "%s optional dependency file %s not found\n", pkg_name, line);
                                break;
                        }

                        if (dep->optional == NULL) {
                                dep->optional =
                                    bds_stack_alloc(1, sizeof(struct dep *), (void (*)(void *))dep_free);
                        }
                        bds_stack_push(dep->optional, &opt_dep);
                } break;
                case BUILDOPTS_BLOCK: {
                        char *comment = bds_string_find(line, "#");
                        if (comment) {
                                *comment = '\0';
                        }

                        char *buildopt = bds_string_dup(bds_string_atrim(line));
                        if (dep->buildopts == NULL) {
                                dep->buildopts = bds_stack_alloc(1, sizeof(char *), (void (*)(void *))free_string);
                        }
                        bds_stack_push(dep->buildopts, &buildopt);
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
        if (line != NULL) {
                free(line);
        }

finish:
        if (fp)
                fclose(fp);
        free(dep_file);
        --level;

        return dep;
}

void __print_deps(const struct bds_stack *deps)
{
        if (deps) {
                size_t n             = bds_stack_size(deps);
                const struct dep **d = (const struct dep **)bds_stack_ptr(deps);
                for (size_t i = 0; i < n; ++i) {
                        print_depfile(d[i]);
                }
        }
}

void print_depfile(const struct dep *dep)
{
        __print_deps(dep->required);
        __print_deps(dep->optional);

        printf("%s ", dep->pkg_name);
        if (dep->buildopts) {
                size_t n               = bds_stack_size(dep->buildopts);
                const char **buildopts = (const char **)bds_stack_ptr(dep->buildopts);
                for (size_t i = 0; i < n; ++i) {
                        printf("%s ", buildopts[i]);
                }
        }
        printf("\n");
}

/* void __write_deps_list(FILE *fp, const struct bds_stack *deps) */
/* { */
/*         if (!deps) */
/*                 return; */

/*         const struct dep **d     = (const struct dep **)bds_stack_ptr(deps); */
/*         const struct dep **d_end = d + bds_stack_size(deps); */

/*         for (; d != d_end; ++d) { */
/*                 write_deplist(fp, *d); */
/*         } */
/* } */

void __append_deps(const struct bds_stack *deps, struct bds_stack *dl_list )
{
	if(!deps)
		return;

	size_t n             = bds_stack_size(deps);
	const struct dep **d = (const struct dep **)bds_stack_ptr(deps);

	for( size_t i=0; i<n; ++i ) {
		__append_deps(d[i]->required, dl_list);
		__append_deps(d[i]->optional, dl_list);

		char *pkg_name = bds_string_dup(d[i]->pkg_name);
		bds_stack_push(dl_list, &pkg_name);
	}
	
}

struct dep_list *dep_list_alloc(const char *pkg_name)
{
	struct dep_list *dep_list = calloc(1, sizeof(*dep_list));
	
	dep_list->pkg_name = bds_string_dup(pkg_name);
	dep_list->dep_list = bds_stack_alloc(1, sizeof(char *), (void (*)(void *))free_string);

	return dep_list;
}
void dep_list_free(struct dep_list **dep_list)
{
	if( *dep_list == NULL )
		return;

	free((*dep_list)->pkg_name);
	bds_stack_free(&(*dep_list)->dep_list);
	free(*dep_list);
	*dep_list = NULL;
}

int compar_string_list(const void *a, const void *b)
{
	return strcmp(*((const char **)a), *((const char **)b));
}

struct dep_list *load_dep_list(const char *depdir, const char *pkg_name)
{
	struct dep_list *dep_list = dep_list_alloc(pkg_name);
	
	struct dep *dep = load_depfile(depdir, pkg_name, true);
	assert(dep);

	__append_deps(dep->required, dep_list->dep_list);
	__append_deps(dep->optional, dep_list->dep_list);

	// De-duplicate deps
	struct bds_stack *__dep_list = bds_stack_alloc(1, sizeof(char *), (void (*)(void *))free_string);

	char *d;
	while( bds_stack_pop(dep_list->dep_list, &d) ) {
		const char **p = (const char **)bds_stack_lsearch(__dep_list, &d, compar_string_list);
		if( !p ) {
			char *__pkg_name = bds_string_dup(d);
			bds_stack_push(__dep_list, &__pkg_name);
		}
		free(d);
	}

	while( bds_stack_pop(__dep_list, &d) ) {
		bds_stack_push(dep_list->dep_list, &d);
	}
	bds_stack_free(&__dep_list);

	return dep_list;
}

void write_dep_list(FILE *fp, const struct dep_list *dep_list)
{
	fprintf(fp, "%s:", dep_list->pkg_name);
	size_t n             = bds_stack_size(dep_list->dep_list);
	const char **pkg_name = (const char **)bds_stack_ptr(dep_list->dep_list);

	for(size_t i=0; i<n; ++i ) {
		fprintf(fp, " %s", pkg_name[i]);
	}
	fprintf(fp, "\n");
}

int write_depdb(const char *depdir, const struct bds_stack *pkglist)
{
        int rc   = 0;
        FILE *fp = fopen(DEPDB, "w");
        if (fp == NULL) {
                rc = 1;
                goto finish;
        }

        const struct pkg *pkg     = (const struct pkg *)bds_stack_ptr(pkglist);
        const struct pkg *pkg_end = pkg + bds_stack_size(pkglist);

        for (; pkg != pkg_end; ++pkg) {
                struct dep_list *dep_list = load_dep_list(depdir, pkg->name);
                if (dep_list == NULL) {
                        rc = 2;
                        goto finish;
                }
                write_dep_list(fp, dep_list);
		dep_list_free(&dep_list);
        }

finish:
        if (fp)
                fclose(fp);

        return rc;
}

/* int compar_parents(const void *a, const void *b) */
/* { */
/* 	return strcmp( ((const struct pkg_parents *)a)->pkg_name, ((const struct pkg_parents *)b)->pkg_name); */
/* } */


/* void __add_parents() {const struct bds_stack *deps, struct bds_queue *parent_queue, size_t parents_list_size, struct pkg_parents *parents_list) */
/* { */
/* 	if(!deps) */
/* 		return; */

/* 	size_t n             = bds_stack_size(deps); */
/* 	const struct dep **d = (const struct dep **)bds_stack_ptr(deps); */

/* 	for( i =0; i<n; ++i ) { */
/* 		struct pkg_parents key = { .pkg_name = d[i]->pkg_name }; */
/* 		struct pkg_parents *parents = bsearch(&key, parents_list, parents_list_size, sizeof(struct pkg_parents), compar_parents); */
/* 		if( parents == NULL ) { */
/* 			fprintf(stderr, "unable to find package %s parents\n", d[i]->pkg_name); */
/* 			exit(EXIT_FAILURE); */
/* 		} */
/* 		char *p = bds_string_dup(parent_name); */
/* 		bds_stack_push(parents->parents, &p); */

/* 		p = bds_string_dup(d[i]->pkg_name); */
/* 		bds_queue_push(parent_queue, &p); */
/* 	} */
/* } */


/* void add_parents(const struct dep *dep, struct bds_queue *parent_queue, size_t parents_list_size, struct pkg_parents *parents_list) */
/* { */
/* 	char *pkg_name = bds_string_dup(dep->pkg_name); */
/* 	bds_queue_push(parent_queue, &pkg_name); */

/* 	const char *parent_name = *(const char **)bds_queue_frontptr(parent_queue); */


/* } */

/* int write_parentdb(const char *depdir, const struct bds_stack *pkglist) */
/* { */
/*         int rc   = 0; */
/*         FILE *fp = fopen(PARENTDB, "w"); */
/*         if (fp == NULL) { */
/*                 rc = 1; */
/*                 goto finish; */
/*         } */

/*         const struct pkg *pkg = (const struct pkg *)bds_stack_ptr(pkglist); */
/*         const size_t n        = bds_stack_size(pkglist); */

/*         struct parents parents_list[n]; */

/*         for (size_t i = 0; i < n; ++i) { */
/*                 parents_list[i].pkg_name = bds_string_dup(pkg[i].pkg_name); */
/*                 parents_list[i].parents  = bds_stack_alloc(1, sizeof(char *), (void (*)(void *))free_string); */
/*         } */

/* 	struct bds_queue *parent_queue = bds_queue_alloc(1, sizeof(char *), (void (*)(void *))free_string)); */
	
/*         for (size_t i = 0; i < n; ++i) { */
/*                 const struct dep *dep = load_depfile(depdir, pkg->name, true); */
/*                 if (dep == NULL) { */
/*                         rc = 2; */
/*                         goto finish; */
/*                 } */
/* 		char *p = bds_string_dup(dep->pkg_name); */
/* 		bds_queue_push(parent_queue, &p); */

/* 		add_parents(dep, parent_queue, n, parents_list); */
/*         } */

/* finish: */
/*         if (fp) */
/*                 fclose(fp); */

/*         return rc; */
/* } */
