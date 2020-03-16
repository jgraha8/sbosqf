#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include <libbds/bds_string.h>

#include "file_mmap.h"
#include "slackpkg_repo.h"
#include "user_config.h"

#define SLACKPKG_PKGLIST "/var/lib/slackpkg/pkglist"
#define SLACKPKG_PKGLIST_NCOLS 8
#define SLACKPKG_PKGLIST_PKG_COL 5

static slack_pkg_list_t pkg_cache = NULL;

__attribute__((destructor))
static void __fini();

static int load_repo(const char *repo_name);
static void init_pkg_cache(const char *repo_name);

bool slackpkg_repo_is_installed(const char *pkg_name, const char *tag)
{
	return (slackpkg_repo_search_const(pkg_name, tag) != NULL );
}

const struct slack_pkg *slackpkg_repo_search_const(const char *pkg_name, const char *tag)
{
	if( !pkg_cache ) {
		init_pkg_cache(user_config.slackpkg_repo_name);
		assert(pkg_cache);
	}

	return slack_pkg_list_search_const(pkg_cache, pkg_name, tag);
}

const struct slack_pkg *slackpkg_repo_get_const(size_t i, const char *tag)
{
	if( !pkg_cache ) {
		init_pkg_cache(user_config.slackpkg_repo_name);
		assert(pkg_cache);
	}

	return slack_pkg_list_get_const(pkg_cache, i, tag);
}

ssize_t slackpkg_repo_size()
{
	if( !pkg_cache ) {
		init_pkg_cache(user_config.slackpkg_repo_name);
		assert(pkg_cache);
	}

	return slack_pkg_list_size(pkg_cache);
}

static void init_pkg_cache(const char *repo_name)
{
	assert( !pkg_cache );
	pkg_cache = slack_pkg_list_create();

	if( 0 != load_repo(repo_name) ) {
		fprintf(stderr, "unable to initialize slackpkg repository %s\n", repo_name);
 		exit(EXIT_FAILURE);
	}
}

static int load_repo(const char *repo_name)
{
        struct file_mmap *pkglist_data;
        if ((pkglist_data = file_mmap(SLACKPKG_PKGLIST)) == NULL) {
		fprintf(stderr, "unable to load " SLACKPKG_PKGLIST "\n");
		return 1;
        }

	bds_vector_clear(pkg_cache);

	char **lines;
	size_t num_lines;

	bds_string_tokenize(pkglist_data->data, "\n", &num_lines, &lines);

	// For every line split into columns
	for( size_t n=0; n<num_lines; ++n ) {
		char **cols;
		size_t num_cols;

		bds_string_tokenize(lines[n], " ", &num_cols, &cols);
		assert(SLACKPKG_PKGLIST_NCOLS == num_cols);

		if( strcmp(repo_name, cols[0]) != 0 ) {
			goto cycle;
		}

		struct slack_pkg pkg = slack_pkg_parse(cols[SLACKPKG_PKGLIST_PKG_COL]);
		slack_pkg_list_append(pkg_cache, &pkg);

	cycle:
		free(cols);
	}

	slack_pkg_list_qsort(pkg_cache);
	file_munmap(&pkglist_data);

	return 0;
}

static void __fini()
{
	if( pkg_cache ) {
		slack_pkg_list_destroy(&pkg_cache);
	}
}
