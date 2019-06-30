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
static void load_user_config();

void user_config_init()
{
        user_config = default_user_config();
        load_user_config();
}

/* void fini_user_config() */
/* { */
/* 	destoy_user_config(&user_config); */
/* } */

void user_config_destroy()
{
        free(user_config.sbopkg_repo);
        free(user_config.depdir);
        free(user_config.sbo_tag);
        if (user_config.pager) {
                free(user_config.pager);
        }
        free(user_config.editor);
}

static struct user_config default_user_config()
{
        struct user_config cs = {.sbopkg_repo = bds_string_dup(SBOPKG_REPO),
                                 .depdir      = bds_string_dup(DEPDIR),
                                 .sbo_tag     = bds_string_dup(SBO_TAG),
                                 .pager       = NULL, /* bds_string_dup(PAGER), */
                                 .editor      = bds_string_dup(EDITOR)};
        return cs;
}

#define SET_CONFIG(c, field, value)                                                                               \
        {                                                                                                         \
                if (NULL != (c).field) {                                                                          \
                        free((c).field);                                                                          \
                }                                                                                                 \
                (c).field = bds_string_dup(value);                                                                \
        }

static void load_user_config()
{
        struct stat sb;
        char *home   = NULL;
        char *pager  = NULL;
        char *editor = NULL;
        char *config = NULL;
        FILE *fp     = NULL;

        int rc = 0;

        home = getenv("HOME");
        if (home == NULL) {
                perror("unable to get HOME environment variable");
                exit(EXIT_FAILURE);
        }

        pager = getenv("PAGER");
        if (pager) {
                SET_CONFIG(user_config, pager, pager);
        }

        editor = getenv("EDITOR");
        if (editor) {
                SET_CONFIG(user_config, editor, editor);
        }

        config = bds_string_dup_concat(3, home, "/", CONFIG);

        if (stat(config, &sb) == -1) {
                goto cleanup;
        }

        fp = fopen(config, "r");
        if (fp == NULL) {
                char buf[4096] = {0};
                snprintf(buf, 4095, "unable to open %s", config);
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
                        fprintf(stderr, "badly formatted entry at line %d in %s\n", lineno, config);
                        rc = 1;
                        goto cleanup;
                }

                bds_string_atrim(keyval[0]);
                bds_string_atrim(keyval[1]);

                strip_quotes(keyval[1]);

                if (strcmp(keyval[0], "SBOPKG_REPO") == 0) {
                        SET_CONFIG(user_config, sbopkg_repo, keyval[1]);
                } else if (strcmp(keyval[0], "SBO_TAG") == 0) {
                        SET_CONFIG(user_config, sbo_tag, keyval[1]);
                } else if (strcmp(keyval[0], "DEPDIR") == 0) {
                        SET_CONFIG(user_config, depdir, keyval[1]);
                } else if (strcmp(keyval[0], "PAGER") == 0) {
                        SET_CONFIG(user_config, pager, keyval[1]);
                } else if (strcmp(keyval[0], "EDITOR") == 0) {
                        SET_CONFIG(user_config, editor, keyval[1]);
                } else {
                        fprintf(stderr, "unknown configuration %s=%s at line %d in %s\n", keyval[0], keyval[1],
                                lineno, config);
                }

                free(keyval);
        }

cleanup:
        if (config)
                free(config);
        if (fp)
                fclose(fp);

        if (rc != 0)
                exit(EXIT_FAILURE);

        return;
}
