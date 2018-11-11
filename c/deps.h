#ifndef __DEPS_H__
#define __DEPS_H__

#include <libbds/bds_vector.h>
#include <stdbool.h>

#include "pkglist.h"

typedef struct bds_vector string_stack_t;
typedef struct bds_vector dep_stack_t;
typedef struct bds_vector dep_info_stack_t;

enum dep_file_status {
	DEP_FILE_NONE,
	DEP_FILE_EXISTS,
	DEP_FILE_NOT_FILE
};


struct dep_info {
        char *pkg_name;
        string_stack_t *buildopts;
        bool is_meta;
};

struct dep {
        struct dep_info info;
        dep_stack_t *required;
        dep_stack_t *optional;
};

struct dep_list {
        struct dep_info info;
        dep_info_stack_t *dep_list;
};

typedef struct bds_vector dep_parents_stack_t;

struct dep_parents {
        struct dep_info info;
        dep_info_stack_t *parents_list;
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
dep_parents_stack_t *dep_parents_stack_alloc();
void dep_parents_stack_free(dep_parents_stack_t **dps);
struct dep_parents *dep_parents_stack_search(dep_parents_stack_t *dps, const char *pkg_name);

struct dep *load_dep_file(const char *pkg_name, bool recursive, bool optional);

void print_dep_sqf(const struct dep *dep);

void write_deplist(FILE *fp, const struct dep *dep);
int write_depdb(const pkg_stack_t *pkglist, bool recursive, bool optional);

struct dep_list *load_dep_list(const char *pkg_name, bool recursive, bool optional);
struct dep_list *load_dep_list_from_dep(const struct dep *dep);

int write_parentdb(const pkg_stack_t *pkglist, bool recursive, bool optional);

int request_add_dep_file(const char *pkg_name, bool review);
int create_default_dep_file(const char *pkg_name);

const char *find_dep_file(const char *pkg_name);

enum dep_file_status dep_file_status(const char *pkg_name);

int edit_dep_file(const char *pkg_name);
#endif // __DEPS_H__
