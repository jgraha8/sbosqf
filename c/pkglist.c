#define  _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
	 
#include <libbds/bds_string.h>
#include <libbds/bds_stack.h>

#include "config.h"

int compar_pkg(const void *a, const void *b)
{
	return strcmp(*(const char **)a, *(const char **)b);
}

void pkg_dtor(void *pkg)
{
	free(*(char **)pkg);
}

struct bds_stack *load_pkglist(const char *depdir)
{
	struct bds_stack *pkglist = bds_stack_alloc(1, sizeof(char *), pkg_dtor);

	char *pkglist_file = bds_string_dup_concat(2, depdir, "/PKGLIST");
	FILE *fp = fopen(pkglist_file, "r");

	if( fp == NULL ) {
		fprintf(stderr, "%s(%d): unable to open %s\n", __FILE__, __LINE__, pkglist_file);
		exit(EXIT_FAILURE);
	}

	char *line=NULL;
	size_t num_line=0;
	ssize_t num_read=0;
	
	while( (num_read = getline(&line, &num_line, fp)) != -1 ) {
		assert(line);
		
		char *new_line = bds_string_rfind(line, "\n");
		if( new_line )
			*new_line = '\0';

		if( *bds_string_atrim(line) == '\0' ) {
			free(line);
			goto cycle;
		}

		bds_stack_push(pkglist, &line);

	cycle:
		line=NULL;
		num_line=0;
	}
	if( line != NULL ) {
		free(line);
	}

	bds_stack_qsort(pkglist, compar_pkg);

	fclose(fp);
	free(pkglist_file);

	return pkglist;
}
      
void print_pkglist(const struct bds_stack *pkglist)
{
	const char **p = (const char **)bds_stack_ptr(pkglist);
	
	for( size_t i=0; i<bds_stack_size(pkglist); ++i ) {
		printf("%s\n", p[i]);
	}
}
