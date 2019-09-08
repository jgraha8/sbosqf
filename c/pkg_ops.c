#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libbds/bds_queue.h>

#include "file_mmap.h"
#include "mesg.h"
#include "pkg_ops.h"
#include "pkg_util.h"
#include "sbo.h"
#include "user_config.h"

#ifdef MAX_LINE
#undef MAX_LINE
#endif
#define MAX_LINE 2048

#define BORDER1 "================================================================================"
#define BORDER2 "::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::"
#define BORDER3 "--------------------------------------------------------------------------------"

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

                                pkg_init_version(&pkg_node->pkg, sbo_read_version(sbo_dir, pkg_node->pkg.name));
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

                fprintf(fp, "%s:%s:%x", pkg_node->pkg.name, pkg_node->pkg.version, pkg_node->pkg.info_crc);
                if (write_sbo_dir)
                        fprintf(fp, ":%s", pkg_node->pkg.sbo_dir + strlen(user_config.sbopkg_repo) + 1);
                fprintf(fp, "\n");
        }
        fclose(fp);

        return 0;
}

int pkg_write_db(pkg_nodes_t *pkgs)
{
        char db_file[4096];
        bds_string_copyf(db_file, sizeof(db_file), "%s/PKGDB", user_config.depdir);

        return __create_db(db_file, pkgs, true);
}

int pkg_write_reviewed(pkg_nodes_t *pkgs)
{
        char db_file[4096];
        bds_string_copyf(db_file, sizeof(db_file), "%s/REVIEWED", user_config.depdir);

        return __create_db(db_file, pkgs, false);
}

int pkg_create_default_deps(pkg_nodes_t *pkgs)
{
        // printf("creating default dependency files...\n");
        for (size_t i = 0; i < bds_vector_size(pkgs); ++i) {
                struct pkg_node *pkg_node = *(struct pkg_node **)bds_vector_get(pkgs, i);

                if (dep_file_exists(pkg_node->pkg.name))
                        continue;

                const char *dep_file = NULL;
                if ((dep_file = create_default_dep_verbose(&pkg_node->pkg)) == NULL) {
                        mesg_error("unable to create %s dependency file\n", pkg_node->pkg.name);
                }
        }
        return 0;
}

int pkg_compar_sets(const pkg_nodes_t *new_pkgs, pkg_nodes_t *old_pkgs)
{
        size_t num_nodes = 0;

        // printf("comparing new and previous package sets...\n");

        struct bds_queue *upgraded_pkg_queue   = bds_queue_alloc(1, sizeof(struct pkg[2]), NULL);
        struct bds_queue *downgraded_pkg_queue = bds_queue_alloc(1, sizeof(struct pkg[2]), NULL);
        struct bds_queue *modified_pkg_queue   = bds_queue_alloc(1, sizeof(struct pkg), NULL);
        struct bds_queue *added_pkg_queue      = bds_queue_alloc(1, sizeof(struct pkg), NULL);

        bds_queue_set_autoresize(upgraded_pkg_queue, true);
        bds_queue_set_autoresize(downgraded_pkg_queue, true);
        bds_queue_set_autoresize(modified_pkg_queue, true);
        bds_queue_set_autoresize(added_pkg_queue, true);

        num_nodes = pkg_nodes_size(new_pkgs);
        for (size_t i = 0; i < num_nodes; ++i) {
                const struct pkg_node *new_pkg = pkg_nodes_get_const(new_pkgs, i);

                struct pkg_node *old_pkg = pkg_nodes_bsearch(old_pkgs, new_pkg->pkg.name);
                if (old_pkg) {
                        if (old_pkg->pkg.info_crc != new_pkg->pkg.info_crc) {
                                int ver_diff = compar_versions(old_pkg->pkg.version, new_pkg->pkg.version);
                                struct pkg updated_pkg[2] = {old_pkg->pkg, new_pkg->pkg};

                                if (ver_diff == 0) {
                                        bds_queue_push(modified_pkg_queue, &updated_pkg[0]);
                                } else if (ver_diff < 0) {
                                        bds_queue_push(upgraded_pkg_queue, &updated_pkg);
                                } else {
                                        bds_queue_push(downgraded_pkg_queue, &updated_pkg);
                                }
                        }
                        old_pkg->color = COLOR_BLACK;
                } else {
                        bds_queue_push(added_pkg_queue, &new_pkg->pkg);
                }
        }

        struct pkg added_pkg;
        struct pkg mod_pkg;
        struct pkg updated_pkg[2];

        if (bds_queue_size(added_pkg_queue) > 0)
                printf("Added:\n");
        while (bds_queue_pop(added_pkg_queue, &added_pkg)) {
                printf("  [A] %-24s %-8s\n", added_pkg.name, added_pkg.version);
        }
        bds_queue_free(&added_pkg_queue);

        if (bds_queue_size(upgraded_pkg_queue) > 0) {
                printf("Upgraded:\n");
                while (bds_queue_pop(upgraded_pkg_queue, &updated_pkg)) {
                        printf("  [U] %-24s %-8s --> %s\n", updated_pkg[0].name, updated_pkg[0].version,
                               updated_pkg[1].version);
                }
        }
        bds_queue_free(&upgraded_pkg_queue);

        if (bds_queue_size(downgraded_pkg_queue) > 0) {
                printf("Downgraded:\n");
                while (bds_queue_pop(downgraded_pkg_queue, &updated_pkg)) {
                        printf("  [D] %-24s %-8s --> %s\n", updated_pkg[0].name, updated_pkg[0].version,
                               updated_pkg[1].version);
                }
        }
        bds_queue_free(&downgraded_pkg_queue);

        if (bds_queue_size(modified_pkg_queue) > 0) {
                printf("Modified:\n");
                while (bds_queue_pop(modified_pkg_queue, &mod_pkg)) {
                        printf("  [M] %-24s %-8s\n", mod_pkg.name, mod_pkg.version);
                }
        }
        bds_queue_free(&modified_pkg_queue);

        bool have_removed = false;
        num_nodes         = pkg_nodes_size(old_pkgs);
        for (size_t i = 0; i < num_nodes; ++i) {
                const struct pkg_node *old_pkg = pkg_nodes_get_const(old_pkgs, i);
                if (old_pkg->color == COLOR_WHITE) {
                        if (!have_removed) {
                                printf("Removed:\n");
                                have_removed = true;
                        }
                        printf("  [R] %-24s %-8s\n", old_pkg->pkg.name, old_pkg->pkg.version);
                }
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
                        assert(num_tok == 4);
                } else {
                        assert(num_tok == 3);
                }

                struct pkg_node *pkg_node = pkg_node_alloc(tok[0]);

                pkg_init_version(&pkg_node->pkg, tok[1]);
                pkg_node->pkg.info_crc = strtol(tok[2], NULL, 16);

                if (read_sbo_dir) {
                        bds_string_copyf(sbo_dir, sizeof(sbo_dir), "%s/%s", user_config.sbopkg_repo, tok[3]);
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

static int __pkg_review(const struct pkg *pkg, bool include_dep)
{
        const char *sbo_info = sbo_find_info(user_config.sbopkg_repo, pkg->name);
        if (!sbo_info) {
                return -1;
        }

        FILE *fp = stdout;
        if (user_config.pager) {
                fp = popen(user_config.pager, "w");
                if (!fp) {
                        perror("popen()");
                        return -1;
                }
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

        if (include_dep) {
                bds_string_copyf(file_name, sizeof(file_name), "%s/%s", user_config.depdir, pkg->name);
                if ((dep = file_mmap(file_name)) == NULL) {
                        create_default_dep_verbose(pkg);
                        assert(dep = file_mmap(file_name));
                }
        }

        if (stdout != fp) {
                assert(system("clear") == 0);
        }

        // clang-format: off
        fprintf(fp,
                BORDER1 "\n"
                        "%s\n" // package name
                BORDER1 "\n"
                        "\n"
                        "%s\n" // package info
                BORDER2 "\n"
                        "README\n" BORDER2 "\n"
                        "%s\n" // readme file
                        "\n",
                pkg->name, info->data, readme->data);

        if (include_dep) {
                fprintf(fp,
                        BORDER2 "\n"
                                "Dependency File\n" // package name
                        BORDER2 "\n");

                if (dep) {
                        fprintf(fp, "%s\n\n", dep->data);
                } else {
                        fprintf(fp, "%s dependency file not found\n\n", pkg->name);
                }
        }
finish:
        if (stdout != fp) {
                if (pclose(fp) == -1) {
                        perror("pclose()");
                }
        }
        if (readme)
                file_munmap(&readme);
        if (info)
                file_munmap(&info);
        if (dep)
                file_munmap(&dep);

        return 0;
}

int pkg_review(const struct pkg *pkg) { return __pkg_review(pkg, true); }

int pkg_show_info(const struct pkg *pkg) { return __pkg_review(pkg, false); }

static char read_response()
{
        char response[2048] = {0};

        if (fgets(response, sizeof(response) - 1, stdin) == NULL) {
                return -1;
        }

        char *c;

        // Expect newline
        if ((c = bds_string_rfind(response, "\n"))) {
                *c = '\0';
        } else {
                return -1;
        }

        // Expect only one character
        if (response[1])
                return -1;

        return response[0];
}

/*
 * Returns:
 *   -1 on error
 *    0 if dependency is to be added to REVIEWED
 *    1 if dependency is to not be added to REVIEWED
 */
int pkg_review_prompt(const struct pkg *pkg, bool return_on_modify_mask, int *dep_status)
{
        int rc           = 0;
        static int level = 0;

        if (pkg_review(pkg) != 0)
                return -1;

        ++level;
        if (level == 1) {
                *dep_status = 0;
        }

        while (1) {
                printf("Add %s to REVIEWED ([Y]es / [n]o / [d]efault / [e]dit / [a]gain / [q]uit)? ", pkg->name);
                char r = 0;
                if ((r = read_response()) < 0) {
                        continue;
                }
                if (r == 'y' || r == 'Y' || r == '\0') {
                        rc = 0;
                        break;
                }
                if (r == 'n' || r == 'N') {
                        rc = 1;
                        break;
                }
                if (r == 'd' || r == 'D') {
                        // Reset to default dependency file
                        assert(create_default_dep(pkg) != NULL);
                        *dep_status |= PKG_DEP_REVERTED_DEFAULT;
                        if (*dep_status & return_on_modify_mask) {
                                rc = 1;
                                break;
                        }
                        rc = pkg_review_prompt(pkg, return_on_modify_mask, dep_status);
                        break;
                }
                if (r == 'e' || r == 'E') {
                        if (0 != edit_dep_file(pkg->name))
                                exit(EXIT_FAILURE);
                        *dep_status |= PKG_DEP_EDITED;
                        if (*dep_status & return_on_modify_mask) {
                                rc = 1;
                                break;
                        }
                        rc = pkg_review_prompt(pkg, return_on_modify_mask, dep_status);
                        break;
                }
                if (r == 'a' || r == 'A') {
                        rc = pkg_review_prompt(pkg, return_on_modify_mask, dep_status);
                        break;
                }
                if (r == 'q' || r == 'Q') {
                        mesg_error("terminating upon user request\n");
                        exit(EXIT_FAILURE);
                }
        }

        --level;

        return rc;
}
