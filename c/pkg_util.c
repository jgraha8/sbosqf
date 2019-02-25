#include <assert.h>
#include <stdio.h>

#include "sbo.h"
#include "pkg_util.h"
#include "slack_pkg.h"
#include "user_config.h"

enum block_type { NO_BLOCK, REQUIRED_BLOCK, OPTIONAL_BLOCK, BUILDOPTS_BLOCK };

bool skip_installed(const char *pkg_name, struct pkg_options options)
{
        if (options.check_installed) {
                const char *check_tag =
                    (options.check_installed & PKG_CHECK_ANY_INSTALLED ? NULL : user_config.sbo_tag);
                if (slack_pkg_is_installed(pkg_name, check_tag)) {
                        return true;
                }
        }
        return false;
}

int load_dep_file(pkg_list_t *pkg_list, const char *pkg_name, struct pkg_options options)
{
        static int level = 0;

        struct pkg *pkg = pkg_list_bsearch(pkg_list, pkg_name);
        if (pkg == NULL)
                return 1;

        int rc          = 0;
        char *line      = NULL;
        size_t num_line = 0;
        char *dep_file  = bds_string_dup_concat(3, user_config.depdir, "/", pkg->name);
        FILE *fp        = fopen(dep_file, "r");

        ++level;

        if (fp == NULL) {
                // Create the default dep file (don't ask just do it)
                if (create_default_dep(pkg) == NULL) {
                        rc = 1;
                        goto finish;
                }

                fp = fopen(dep_file, "r");
                if (fp == NULL) {
                        rc = 1;
                        goto finish;
                }
        }

        enum block_type block_type = NO_BLOCK;

        while (getline(&line, &num_line, fp) != -1) {
                assert(line);

                // Trim newline
                char *c = line;
                while (*c) {
                        if (*c == '\n' || *c == '\t' || *c == '\\') {
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
                        pkg->dep.is_meta = true;
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
                if (!pkg->dep.is_meta && !options.recursive && level > 1)
                        goto finish;

                switch (block_type) {
                case OPTIONAL_BLOCK:
                        if (!options.optional)
                                break;
                case REQUIRED_BLOCK: {
                        if (skip_installed(line, options))
                                break;

                        assert(load_dep_file(pkg_list, line, options) == 0);

                        struct pkg *req = pkg_list_bsearch(pkg_list, line);
                        assert(req);

			if( options.revdeps ) 
				pkg_append_parent(req, pkg);
                        pkg_append_required(pkg, req);

                } break;
                case BUILDOPTS_BLOCK: {
                        char *buildopt = bds_string_dup(bds_string_atrim(line));
                        pkg_append_buildopts(pkg, buildopt);
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

        return rc;
}

const char *create_default_dep(struct pkg *pkg)
{
        if (pkg->sbo_dir == NULL)
                return NULL;

        static char dep_file[4096];

        FILE *fp            = NULL;
        char **required     = NULL;
        size_t num_required = 0;

        const char *rp = NULL;

        const char *sbo_requires = sbo_read_requires(pkg->sbo_dir, pkg->name);
        if (!sbo_requires) {
                goto finish;
        }

        bds_string_copyf(dep_file, sizeof(dep_file), "%s/%s", user_config.depdir, pkg->name);

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
