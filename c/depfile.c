#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#include <libbds/bds_string.h>
#include <libbds/bds_stack.h>

#include "depfile.h"

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

struct dep *load_depfile(const char *depdir, const char *pkg_name)
{
        char *dep_file = bds_string_dup_concat(3, depdir, "/", pkg_name);
        FILE *fp       = fopen(dep_file, "r");

        if (fp == NULL) {
                return NULL;
        }

	struct dep *dep = dep_alloc(pkg_name);

        char *line     = NULL;
        size_t num_line = 0;

	enum block_type block_type = NO_BLOCK;

        while (getline(&line, &num_line, fp) != -1) {
                assert(line);

                char *new_line = bds_string_rfind(line, "\n");
                if (new_line)
                        *new_line = '\0';

                if (*bds_string_atrim(line) == '\0') {
                        goto cycle;
                }

                if (*line == '#' || *line == '-') {
                        goto cycle;
                }

		if( strcmp(line, "METAPKG") == 0 ) {
			dep->is_meta = true;
			goto cycle;
		}

		if( strcmp(line, "REQUIRED:") == 0 ) {
			block_type = REQUIRED_BLOCK;
			goto cycle;
		}

		if( strcmp(line, "OPTIONAL:") == 0 ) {
			block_type = OPTIONAL_BLOCK;
			goto cycle;
		}

		if( strcmp(line, "BUILDOPTS:") == 0 ) {
			block_type = BUILDOPTS_BLOCK;
			goto cycle;
		}

		switch(block_type) {
		case REQUIRED_BLOCK:
		{
			struct dep *req_dep = load_depfile(depdir, line);
			if( dep->required == NULL ) {
				dep->required = bds_stack_alloc(1, sizeof(struct dep *), (void (*)(void *))dep_free);
			}
			bds_stack_push(dep->required, &req_dep);
		}
		break;
		case OPTIONAL_BLOCK:
		{
			struct dep *opt_dep = load_depfile(depdir, line);
			if( dep->optional == NULL ) {
				dep->optional = bds_stack_alloc(1, sizeof(struct dep *), (void (*)(void *))dep_free);
			}
			bds_stack_push(dep->optional, &opt_dep);
		}
		break;
		case BUILDOPTS_BLOCK:
		{
			char *comment = bds_string_find(line, "#");
			if( comment ) {
				*comment = '\0';
			}
			
			char *buildopt = bds_string_dup(bds_string_atrim(line));
			if( dep->buildopts == NULL ) {
				dep->buildopts = bds_stack_alloc(1, sizeof(char *), (void (*)(void *))free_string);
			}
			bds_stack_push(dep->buildopts, &buildopt);			
		}
		break;
		default:
			fprintf(stderr, "%s(%d): badly formatted dependency file %s\n", __FILE__, __LINE__, dep_file);
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

	fclose(fp);
	free(dep_file);

	return dep;
}

void __print_dep_list(const struct bds_stack *deps)
{
	if( deps ) {
		size_t n = bds_stack_size(deps);
		const struct dep **d = (const struct dep **)bds_stack_ptr(deps);
		for( size_t i=0; i<n; ++i ) {
			print_depfile(d[i]);
		}
	}
}

void print_depfile(const struct dep *dep)
{
	__print_dep_list(dep->required);
	__print_dep_list(dep->optional);

	printf("%s ", dep->pkg_name);
	if( dep->buildopts ) {
		size_t n = bds_stack_size(dep->buildopts);
		const char **buildopts = (const char **)bds_stack_ptr(dep->buildopts);
		for( size_t i=0; i<n; ++i ) {
			printf("%s ", buildopts[i]);
		}
	}
	printf("\n");
}
