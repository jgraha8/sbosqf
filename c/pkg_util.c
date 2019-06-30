#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libbds/bds_queue.h>
#include <libbds/bds_stack.h>
#include <libbds/bds_string.h>

#include "config.h"
#include "file_mmap.h"
#include "filevercmp.h"
#include "pkg_ops.h"
#include "pkg_util.h"
#include "sbo.h"
#include "slack_pkg.h"
#include "user_config.h"

enum block_type { NO_BLOCK, REQUIRED_BLOCK, OPTIONAL_BLOCK, BUILDOPTS_BLOCK };

void free_string_ptr(char **str)
{
        if (*str == NULL)
                return;
        free(*str);
        *str = NULL;
}

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

static bool skip_dep_line(char *line)
{
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
                return true;
        }

        if (*line == '-') {
                return true;
        }

        return false;
}

bool is_meta_pkg(const char *pkg_name)
{
        bool is_meta = false;

        // Load meta pkg dep file
        char dep_file[2048];
        struct file_mmap *dep = NULL;

        bds_string_copyf(dep_file, sizeof(dep_file), "%s/%s", user_config.depdir, pkg_name);
        if ((dep = file_mmap(dep_file)) == NULL)
                return false;

        char *line     = dep->data;
        char *line_end = NULL;

        while ((line_end = bds_string_find(line, "\n"))) {
                *line_end = '\0';

                if (skip_dep_line(line))
                        goto cycle;

                if (strcmp(line, "METAPKG") == 0) {
                        is_meta = true;
                        break;
                }
        cycle:
                line = line_end + 1;
        }
        file_munmap(&dep);

        return is_meta;
}

int __load_dep(struct pkg_graph *pkg_graph, struct pkg_node *pkg_node, struct pkg_options options)
{
        int rc = 0;

        char *line      = NULL;
        size_t num_line = 0;
        char *dep_file  = NULL;
        FILE *fp        = NULL;

        dep_file = bds_string_dup_concat(3, user_config.depdir, "/", pkg_node->pkg.name);
        fp       = fopen(dep_file, "r");

        if (fp == NULL) {
                // Create the default dep file (don't ask just do it)
                if (create_default_dep_verbose(&pkg_node->pkg) == NULL) {
                        rc = 1;
                        goto finish;
                }

                fp = fopen(dep_file, "r");
                if (fp == NULL) {
                        rc = 1;
                        goto finish;
                }
        }

        pkg_node->color = COLOR_GREY;

        enum block_type block_type = NO_BLOCK;

        while (getline(&line, &num_line, fp) != -1) {
                assert(line);

                if (skip_dep_line(line))
                        goto cycle;

                if (strcmp(line, "METAPKG") == 0) {
                        assert(pkg_node->pkg.dep.is_meta);
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
                 * Recursive processing will occur on meta packages since they act as "include" files. We only
                 * check the recursive flag if the dependency file is not marked as a meta package.
                 */
                if (!pkg_node->pkg.dep.is_meta && !options.recursive)
                        goto finish;

                switch (block_type) {
                case OPTIONAL_BLOCK:
                        if (!options.optional)
                                break;
                case REQUIRED_BLOCK: {
                        if (skip_installed(line, options))
                                break;

                        struct pkg_node *req_node = pkg_graph_search(pkg_graph, line);

                        if (req_node == NULL) {
                                fprintf(stderr, COLOR_WARN "[warning]" COLOR_END ": %s no longer in repository\n",
                                        line);
                                break;
                        }
                        if (req_node->color == COLOR_GREY) {
                                fprintf(stderr, "error: load_dep: cyclic dependency found: %s <--> %s\n",
                                        pkg_node->pkg.name, req_node->pkg.name);
                                exit(EXIT_FAILURE);
                        }

                        if (options.revdeps)
                                pkg_insert_parent(&req_node->pkg, pkg_node);

                        pkg_insert_required(&pkg_node->pkg, req_node);

                        /* Avoid revisiting nodes more than once */
                        if (req_node->color == COLOR_WHITE)
                                __load_dep(pkg_graph, req_node, options);

                } break;
                case BUILDOPTS_BLOCK: {
                        char *buildopt = bds_string_dup(bds_string_atrim(line));
                        pkg_append_buildopts(&pkg_node->pkg, buildopt);
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
        pkg_node->color = COLOR_BLACK;

        if (line != NULL) {
                free(line);
        }

        if (fp)
                fclose(fp);
        free(dep_file);

        return rc;
}

int load_dep_file(struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options options)
{
        struct pkg_node *pkg_node = pkg_graph_search(pkg_graph, pkg_name);
        if (pkg_node == NULL)
                return 1;

        pkg_graph_clear_markers(pkg_graph);
        int rc = __load_dep(pkg_graph, pkg_node, options);

        return rc;
}

bool file_exists(const char *pathname)
{
        struct stat sb;

        if (stat(pathname, &sb) == -1)
                return false;

        if (!S_ISREG(sb.st_mode))
                return false;

        return true;
}

bool dep_file_exists(const char *pkg_name)
{
        char dep_file[4096];

        bds_string_copyf(dep_file, sizeof(dep_file), "%s/%s", user_config.depdir, pkg_name);

        return file_exists(dep_file);
}

const char *create_default_dep(const struct pkg *pkg)
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

const char *create_default_dep_verbose(const struct pkg *pkg)
{
        const char *dep_file = NULL;
        if ((dep_file = create_default_dep(pkg)) != NULL) {
                printf("  created %s\n", dep_file);
        } else {
                fprintf(stderr, "  unable to create %s dependency file\n", pkg->name);
        }
        return dep_file;
}

static void write_buildopts(FILE *fp, const struct pkg *pkg)
{
        const size_t n = pkg_buildopts_size(pkg);

        for (size_t i = 0; i < n; ++i) {
                fprintf(fp, " %s", pkg_buildopts_get_const(pkg, i));
        }
}

void check_reviewed_pkg(const struct pkg *pkg, bool auto_add, pkg_nodes_t *reviewed_pkgs,
                        bool *reviewed_pkgs_dirty)
{
        bool review_pkg                = false;
        struct pkg_node *reviewed_node = pkg_nodes_bsearch(reviewed_pkgs, pkg->name);
        if (reviewed_node) {
                if (reviewed_node->pkg.info_crc != pkg->info_crc)
                        review_pkg = true;
        } else {
                review_pkg = true;
        }

        if (review_pkg) {
                int rc = (auto_add ? pkg_review(pkg) : pkg_review_prompt(pkg));

                if (rc == 0) {
                        *reviewed_pkgs_dirty = true;
                        if (reviewed_node) {
                                pkg_copy_nodep(&reviewed_node->pkg, pkg);
                        } else {
                                struct pkg_node *pkg_node = pkg_node_alloc(pkg->name);
                                pkg_copy_nodep(&pkg_node->pkg, pkg);

                                pkg_nodes_insert_sort(reviewed_pkgs, pkg_node);
                        }
                }
        }
}

void write_sqf(FILE *fp, struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options options,
               pkg_nodes_t *reviewed_pkgs, bool *reviewed_pkgs_dirty)
{
        struct pkg_iterator iter;

        pkg_iterator_flags_t flags = 0;
        int max_dist               = (options.deep ? -1 : 1);

        struct bds_stack *revdeps_pkgs = NULL;

        if (options.revdeps) {
                flags        = PKG_ITER_REVDEPS;
                revdeps_pkgs = bds_stack_alloc(1, sizeof(struct pkg), NULL);
        } else {
                flags = PKG_ITER_DEPS;
        }

        for (struct pkg_node *node = pkg_iterator_begin(&iter, pkg_graph, pkg_name, flags, max_dist); node != NULL;
             node                  = pkg_iterator_next(&iter)) {

                if (node->pkg.dep.is_meta)
                        continue;

                if (options.check_installed) {
                        const char *tag =
                            (options.check_installed & PKG_CHECK_ANY_INSTALLED ? NULL : user_config.sbo_tag);
                        if (slack_pkg_is_installed(node->pkg.name, tag))
                                continue;
                }

                check_reviewed_pkg(&node->pkg, options.reviewed_auto_add, reviewed_pkgs, reviewed_pkgs_dirty);

                if (options.revdeps) {
                        bds_stack_push(revdeps_pkgs, &node->pkg);
                } else {
                        fprintf(fp, "%s", node->pkg.name);
                        if (fp == stdout) {
                                fprintf(fp, " ");
                        } else {
                                write_buildopts(fp, &node->pkg);
                                fprintf(fp, "\n");
                        }
                }
        }

        if (options.revdeps) {
                struct pkg pkg;
                while (bds_stack_pop(revdeps_pkgs, &pkg)) {
                        fprintf(fp, "%s", pkg.name);
                        if (fp == stdout) {
                                fprintf(fp, " ");
                        } else {
                                write_buildopts(fp, &pkg);
                                fprintf(fp, "\n");
                        }
                }
                bds_stack_free(&revdeps_pkgs);
        }
        if (fp == stdout)
                fprintf(fp, "\n");

        pkg_iterator_destroy(&iter);
}

int compar_versions(const char *ver_a, const char *ver_b) { return filevercmp(ver_a, ver_b); }

#if 0
void write_remove_sqf(FILE *fp, struct pkg_graph *pkg_graph, const char *pkg_name, struct pkg_options options)
{
        struct pkg_iterator deps_iter, revdeps_iter;

        const char *tag = (options.check_installed & PKG_CHECK_ANY_INSTALLED ? NULL : user_config.sbo_tag);

	// Walk the dep tree



        for (struct pkg_node *dep_node  = pkg_iterator_begin(&deps_iter, pkg_graph, pkg_name, PKG_ITER_DEPS, -1);
             dep_node != NULL; dep_node = pkg_iterator_next(&iter)) {
		// Mark all packages for removal
		dep_node->pkg.for_removal = true;
	}

	pkg_iterator_destroy(&deps_iter);

        for (struct pkg_node *dep_node  = pkg_iterator_begin(&deps_iter, pkg_graph, pkg_name, PKG_ITER_DEPS, -1);
             dep_node != NULL; dep_node = pkg_iterator_next(&deps_iter)) {

		/*
		  Check immediate parent packages to see if any are installed
		*/
                for (struct pkg_node *revdep_node =
			     pkg_iterator_begin(&revdeps_iter, pkg_graph, dep_node->pkg.name, PKG_ITER_REVDEPS, 1);
                     revdep_node != NULL; revdep_node = pkg_iterator_next(&revdesp_iter)) {

			struct pkg_node *rnode =  pkg_iterator_node(&revdeps_iter);
			
			if( rnode == NULL ) {
				assert( strcmp(rnode->pkg.name, dep_node->pkg.name) == 0 );
				continue;
			}
			assert( dep_node == rnode );

			if (!rnode->pkg.parent_installed) {
                                if (!revdep_node->pkg.for_removal && slack_pkg_is_installed(revdep_node->pkg.name, tag)) {
                                        printf("%s is required by at least %s\n", rnode->pkg.name,
                                               revdep_node->pkg.name);
                                        rnode->pkg.parent_installed = true;
                                }
                        }
			
		}

		/*
		  If a package has at least one parent installed
		  whichi is not marked for removal, then unmark the
		  package for removal.
		 */
		if( dep_node->pkg.parent_installed ) {
			dep_node->pkg.for_removal = false;
		
		pkg_iterator_destroy(&revdeps_iter);
	}
                        if (node->pkg.dep.is_meta)
                                continue;

                        struct pkg_node *req_node = pkg_iterator_node(&iter);

                        if (!req_node) {
                                assert(strcmp(pkg_name, node->pkg.name) == 0);
                                goto write_pkg;
                        }

                        if (!req_node->pkg.parent_installed) {
                                if (slack_pkg_is_installed(node->pkg.name, tag)) {
                                        printf("%s is required by at least %s\n", req_node->pkg.name,
                                               node->pkg.name);
                                        req_node->pkg.parent_installed = true;
                                }
                        }

                write_pkg:
                        if (!node->pkg.parent_installed) {
                                fprintf(fp, "%s", node->pkg.name);
                                if (fp != stdout) {
                                        fprintf(fp, "\n");
                                }
                        }
                }

                pkg_iterator_destroy(&iter);

                return 0;
        }
#endif
const char *find_dep_file(const char *pkg_name)
{
        static char dep_file[4096];
        struct stat sb;

        bds_string_copyf(dep_file, sizeof(dep_file), "%s/%s", user_config.depdir, pkg_name);

        if (stat(dep_file, &sb) == 0) {
                if (sb.st_mode & S_IFREG) {
                        return dep_file;
                } else if (sb.st_mode & S_IFDIR) {
                        fprintf(stderr, "dependency file for package %s already exists as directory: %s\n",
                                pkg_name, dep_file);
                } else {
                        fprintf(stderr, "dependency file for package %s already exists as non-standard file: %s\n",
                                pkg_name, dep_file);
                }
        }
        return NULL;
}

static void sigchld_handler(int signo) { fprintf(stderr, "caught signal %d\n", signo); }

int edit_dep_file(const char *pkg_name)
{
        int rc = 0;
        pid_t cpid;
        struct sigaction sa;

        memset(&sa, 0, sizeof(sa));

        sa.sa_handler = SIG_IGN;
        sa.sa_flags   = 0;
        sigemptyset(&sa.sa_mask); // Not masking SIGINT and SIGQUIT since they will be ignored
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGQUIT, &sa, NULL);

        sa.sa_handler = sigchld_handler;
        // sa.sa_flags   = SA_NOCLDSTOP; // do not receive notification when child process6es stop or resume
        sigaction(SIGCHLD, &sa, NULL);

        cpid = fork();
        if (-1 == cpid) {
                fprintf(stderr, COLOR_FAIL "[error]" COLOR_END ": unable to fork editor process\n");
                return 1;
        }

        if (cpid == 0) {
                // Re-enable default signal handling
                sa.sa_handler = SIG_DFL;
                sa.sa_flags   = 0;
                sigaction(SIGINT, &sa, NULL);
                sigaction(SIGQUIT, &sa, NULL);

                char *dep_file = NULL;
                if (!find_dep_file(pkg_name)) {
                        fprintf(stderr, "[error]" COLOR_END ": unable to find dependency file for %s\n", pkg_name);
                        exit(EXIT_FAILURE);
                }
                dep_file = bds_string_dup(find_dep_file(pkg_name));

                size_t num_tok;
                char **tok;

                char *editor = bds_string_dup(user_config.editor);
                bds_string_tokenize(editor, " ", &num_tok, &tok);
                assert(1 <= num_tok);

                const size_t num_args = num_tok + 2;
                char *args[num_args];

                for (size_t i = 0; i < num_args - 2; ++i) {
                        assert(0 < strlen(tok[i]));
                        args[i] = tok[i];
                }
                args[num_args - 2] = dep_file;
                args[num_args - 1] = NULL;

                if (execvp(args[0], args) == -1) {
                        perror("execvp()");
                        rc = errno;
                }

                // We shouldn't get here, but if we do, clean-up (not really necessary, but good practice)
                free(tok);
                free(editor);

                exit(rc);
        }

        while (1) {
                pid_t pid;
		int wstatus = 0;
		
                fprintf(stderr, "child wait loop...\n");
                if ((pid = waitpid(cpid, &wstatus, WUNTRACED | WCONTINUED)) == -1) {
                        perror("waitpid()");

                        if (EINTR == errno) { // waitpid interrupted
                                fprintf(stderr, "interrupted...\n");
                                continue;
                        }
                        if (ECHILD == errno) { // child already exited
                                fprintf(stderr, "child already exited...\n");
                                rc = errno;
                                break;
                        }
                }
		
                if (WIFSTOPPED(wstatus)) { // Child stopped
                        fprintf(stderr, "child stopped...\n");
                        continue;
                }
                if (WIFCONTINUED(wstatus)) { // Child resumed
                        fprintf(stderr, "child resumed...\n");
                        continue;
                }
                if (WIFSIGNALED(wstatus)) { // Child terminated
                        fprintf(stderr, "child terminated by signal %d\n", WTERMSIG(wstatus));
                        rc = 1;
                        break;
                }
                if (WIFEXITED(wstatus)) { // Child exited normally
                        rc = WEXITSTATUS(wstatus); // Exit status of child
                        fprintf(stderr, "child exited with status %d\n", rc);
                        break;
                }
        }

	// Restore default signal handling
        sa.sa_handler = SIG_DFL;
        sa.sa_flags   = 0;
        sigaction(SIGCHLD, &sa, NULL);
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGQUIT, &sa, NULL);

        return rc;
}
