#ifndef __DEPS_H__
#define __DEPS_H__

#include <libbds/bds_vector.h>
#include <stdbool.h>

#include "pkg_db.h"

typedef struct bds_vector string_vector_t;
typedef struct bds_vector dep_vector_t;
typedef struct bds_vector dep_info_vector_t;

enum dep_file_status { DEP_FILE_NONE, DEP_FILE_EXISTS, DEP_FILE_NOT_FILE };
enum dep_review { DEP_REVIEW_NONE, DEP_REVIEW, DEP_REVIEW_REQUEST };

struct dep_info {
	const char *pkg_name;
        string_vector_t *buildopts;
        bool is_meta;
};

struct dep {
        struct dep_info info;
        dep_vector_t *required;
        dep_vector_t *optional;
};

struct dep_list {
        struct dep_info info;
        dep_info_vector_t *dep_list;
};

typedef struct bds_vector dep_parents_vector_t;

struct dep_parents {
        struct dep_info info;
        dep_info_vector_t *parents_list;
};

struct process_options {
	bool recursive;
	bool optional;
	bool revdeps;
	int check_installed;	
};

enum block_type { NO_BLOCK, REQUIRED_BLOCK, OPTIONAL_BLOCK, BUILDOPTS_BLOCK };

struct dep_info dep_info_ctor(const char *pkg_name);

void dep_info_dtor(struct dep_info *dep_info);

struct dep_info dep_info_deep_copy(const struct dep_info *src);

struct dep *dep_alloc(const char *pkg_name);

void dep_free(struct dep **dep);

struct dep_list *dep_list_alloc(const char *pkg_name);

struct dep_list *dep_list_alloc_with_info(const struct dep_info *info);

void dep_list_free(struct dep_list **dep_list);

struct dep_parents *dep_parents_alloc(const char *pkg_name);

void dep_parents_free(struct dep_parents **dp);

struct dep_info *dep_info_vector_search(dep_info_vector_t *div, const char *pkg_name);

dep_parents_vector_t *dep_parents_vector_alloc();

void dep_parents_vector_free(dep_parents_vector_t **dps);

struct dep_parents *dep_parents_vector_search(dep_parents_vector_t *dps, const char *pkg_name);

struct dep *load_dep_file(const char *pkg_name, struct process_options options);

void write_dep_sqf(FILE *fp, const struct dep *dep, struct process_options options);
void write_sqf(FILE *fp, const struct dep_list *dep_list, struct process_options options);

int write_depdb(struct process_options options);

struct dep_list *load_dep_list(const char *pkg_name, struct process_options options);

struct dep_list *load_dep_list_from_dep(const struct dep *dep);

void write_dep_list(FILE *fp, const struct dep_list *dep_list);

struct dep_parents *load_dep_parents(const char *pkg_name, struct process_options options);

int write_parentdb(struct process_options options);

const char *create_default_dep_file(const char *pkg_name);

const char *find_dep_file(const char *pkg_name);

enum dep_file_status dep_file_status(const char *pkg_name);

int edit_dep_file(const char *pkg_name);

int remove_dep_file(const char *pkg_name);

bool skip_installed(const char *pkg_name, struct process_options options);

int perform_dep_action(const char *pkg_name, int action);

int display_dep_menu(const char *pkg_name, const char *msg, int disabled_actions);
#endif // __DEPS_H__
