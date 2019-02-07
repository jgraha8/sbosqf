#ifndef __PKG_DB_H__
#define __PKG_DB_H__

#include <libbds/bds_list.h>

typedef struct bds_list pkg_list_t;

extern pkg_list_t *pkg_db_pkglist;
extern pkg_list_t *pkg_db_reviewed;

struct pkg {
	const char *name;
        int marked; // Used for graph analyses
};

void init_pkg_db();
void fini_pkg_db();

struct pkg create_pkg(const char *name);
void destroy_pkg(struct pkg *pkg);

pkg_list_t *load_pkg_db(const char *db_name);
int write_pkg_db(const pkg_list_t *pkg_db, const char *pkgdb);
void print_pkg_db(const pkg_list_t *pkg_db);
struct pkg *find_pkg(pkg_list_t *pkg_db, const char *pkg_name);

int add_pkg(pkg_list_t *pkg_db, const char *pkgdb, const char *pkg_name);
int request_add_pkg(pkg_list_t *pkg_db, const char *pkgdb, const char *pkg_name);
int remove_pkg(pkg_list_t *pkg_db, const char *db_name, const char *pkg_name);

int request_review_pkg(const char *pkg_name);
int review_pkg(const char *pkg_name);

#endif // __PKG_DB_H__
