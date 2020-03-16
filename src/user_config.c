#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libbds/bds_string.h>

#include "config.h"
#include "user_config.h"

struct user_config user_config;

#define SET_CONFIG(c, field, value)                                                                               \
        {                                                                                                         \
                if (NULL != (c).field) {                                                                          \
                        free((c).field);                                                                          \
                }                                                                                                 \
                (c).field = bds_string_dup(value);                                                                \
        }

static char *strip_quotes(char *str)
{
        if (*str == '"' || *str == '\'') {
                *str                 = ' '; // Change to whitespace
                str[strlen(str) - 1] = ' ';
                bds_string_atrim(str);
        }
        return str;
}

static struct user_config default_user_config();
static void create_user_config(const char *config_path, const struct user_config *user_config);
static void load_user_config(const char *config_path, struct user_config *user_config);

void user_config_init()
{
        struct stat sb;
        char *home        = NULL;
        char *pager       = NULL;
        char *editor      = NULL;
        char *config_path = NULL;

        user_config = default_user_config();

        pager = getenv("PAGER");
        if (pager) {
                SET_CONFIG(user_config, pager, pager);
        }

        editor = getenv("EDITOR");
        if (editor) {
                SET_CONFIG(user_config, editor, editor);
        }

        home = getenv("HOME");
        if (home == NULL) {
                perror("unable to get HOME environment variable");
                exit(EXIT_FAILURE);
        }
        config_path = bds_string_dup_concat(3, home, "/", CONFIG);

        if (stat(config_path, &sb) == -1) { /* No configuration file */
                create_user_config(config_path, &user_config);
        } else {
                load_user_config(config_path, &user_config);
        }

        free(config_path);
}

void user_config_destroy()
{
        free(user_config.sbopkg_repo);
        free(user_config.slackpkg_repo_name);
        free(user_config.depdir);
        free(user_config.sbo_tag);
        if (user_config.pager) {
                free(user_config.pager);
        }
        free(user_config.editor);
}

static struct user_config default_user_config()
{
        struct user_config cs = {.sbopkg_repo        = bds_string_dup(SBOPKG_REPO),
                                 .slackpkg_repo_name = bds_string_dup(SLACKPKG_REPO_NAME),
                                 .depdir             = bds_string_dup(DEPDIR),
                                 .sbo_tag            = bds_string_dup(SBO_TAG),
                                 .pager              = bds_string_dup(PAGER),
                                 .editor             = bds_string_dup(EDITOR)};
        return cs;
}

static void create_user_config(const char *config_path, const struct user_config *user_config)
{
        FILE *fp = fopen(config_path, "w");
        assert(fp);

        fprintf(fp, "# Default sbopkg-dep2sqf configuration\n"
                    "SBOPKG_REPO = %s\n"
                    "SLACKPKG_REPO_NAME = %s\n"
                    "SBO_TAG = %s\n"
                    "DEPDIR = %s\n"
                    "PAGER = %s\n"
                    "EDITOR = %s\n",
                user_config->sbopkg_repo, user_config->slackpkg_repo_name, user_config->sbo_tag,
                user_config->depdir, user_config->pager, user_config->editor);

        fclose(fp);
}

static void load_user_config(const char *config_path, struct user_config *user_config)
{
        FILE *fp = NULL;

        int rc = 0;

        fp = fopen(config_path, "r");
        if (fp == NULL) {
                char buf[4096] = {0};
                snprintf(buf, 4095, "unable to open %s", config_path);
                perror(buf);
                rc = 1;
                goto cleanup;
        }

        char line[LINE_MAX];
        int lineno = 0;

        while (fgets(line, LINE_MAX, fp) != NULL) {
                ++lineno;
                // Remove new line
                char *c = bds_string_rfind(line, "\n");
                if (c)
                        *c = '\0';

                if (strlen(bds_string_atrim(line)) == 0) {
                        continue;
                }

                if (*line == '#')
                        continue;

                char **keyval;
                size_t num_tok;

                bds_string_tokenize(line, "=", &num_tok, &keyval);

                if (num_tok != 2 || keyval == NULL) {
                        fprintf(stderr, "badly formatted entry at line %d in %s\n", lineno, config_path);
                        rc = 1;
                        goto cleanup;
                }

                bds_string_atrim(keyval[0]);
                bds_string_atrim(keyval[1]);

                strip_quotes(keyval[1]);

                if (strcmp(keyval[0], "SBOPKG_REPO") == 0) {
                        SET_CONFIG(*user_config, sbopkg_repo, keyval[1]);
                } else if (strcmp(keyval[0], "SLACKPKG_REPO_NAME") == 0) {
                        SET_CONFIG(*user_config, slackpkg_repo_name, keyval[1]);
                } else if (strcmp(keyval[0], "SBO_TAG") == 0) {
                        SET_CONFIG(*user_config, sbo_tag, keyval[1]);
                } else if (strcmp(keyval[0], "DEPDIR") == 0) {
                        SET_CONFIG(*user_config, depdir, keyval[1]);
                } else if (strcmp(keyval[0], "PAGER") == 0) {
                        SET_CONFIG(*user_config, pager, keyval[1]);
                } else if (strcmp(keyval[0], "EDITOR") == 0) {
                        SET_CONFIG(*user_config, editor, keyval[1]);
                } else {
                        fprintf(stderr, "unknown configuration %s=%s at line %d in %s\n", keyval[0], keyval[1],
                                lineno, config_path);
                }

                free(keyval);
        }

cleanup:
        if (fp)
                fclose(fp);

        if (rc != 0)
                exit(EXIT_FAILURE);

        return;
}
