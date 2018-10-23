/**
 * @file
 * @brief Main program file for sbopkg-dep2sqf
 *
 */

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libbds/bds_string.h>

#include "depfile.h"
#include "pkglist.h"

#define DEPDIR "/var/lib/sbopkg-dep2sqf"
#define PKGFILE

#define BLACK "01;30"
#define RED "01;31"
#define GREEN "01;32"
#define YELLOW "01;33"
#define BLUE '01;34'
#define MAGENTA "01;35"
#define CYAN "01;36"

#define COLOR_OK "\033[${GREEN}m"
#define COLOR_INFO "\033[${YELLOW}m"
#define COLOR_WARN "\033[${MAGENTA}m"
#define COLOR_FAIL "\033[${RED}m"
#define COLOR_END "\033[0m"

// Parameters
#define CONFIG ".sbopkg-dep2sqf"
#define SBOPKG_REPO "/var/lib/sbopkg/SBo"
#define SBO_TAG "_SBo"
#define PKGLIST "PKGLIST"
#define REVIEWED "REVIEWED"
#define PARENTDB "PARENTDB"
#define DEPDB "DEPDB"
#define REQUIRED_BLOCK 1
#define OPTIONAL_BLOCK 2
#define BUILDOPTS_BLOCK 3

static char *sbopkg_repo = SBOPKG_REPO;
static char *depdir      = DEPDIR;
static char *sbo_tag     = SBO_TAG;

// Environment
#define PAGER "less -r"
static char *pager = PAGER;

static char *strip_quotes(char *str)
{
        if (*str == '"' || *str == '\'') {
                *str                 = ' '; // Change to whitespace
                str[strlen(str) - 1] = ' ';
                bds_string_atrim(str);
        }
        return str;
}

static void load_config()
{
        struct stat sb;
        char *home   = NULL;
        char *config = NULL;
        FILE *fp     = NULL;

        int rc = 0;

        home = getenv("HOME");
        if (home == NULL) {
                perror("unable to get HOME environment variable");
                exit(EXIT_FAILURE);
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
                        sbopkg_repo = bds_string_dup(keyval[1]);
                } else if (strcmp(keyval[0], "SBO_TAG") == 0) {
                        sbo_tag = bds_string_dup(keyval[1]);
                } else if (strcmp(keyval[0], "DEPDIR") == 0) {
                        depdir = bds_string_dup(keyval[1]);
                } else if (strcmp(keyval[0], "PAGER") == 0) {
                        pager = bds_string_dup(keyval[1]);
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

int main(int argc, char **argv)
{
        load_config();

        printf("sbopkg_repo = %s\n", sbopkg_repo);
        printf("sbo_tag = %s\n", sbo_tag);
	printf("pager = %s\n", pager);

	struct bds_stack *pkglist = load_pkglist(DEPDIR);
	print_pkglist(pkglist);

	bds_stack_free(&pkglist);

	struct dep *dep = load_depfile(DEPDIR, "ffmpeg");

	printf("===========================\n");
	print_depfile(dep);

	dep_free(&dep);
	
	return 0;
}
