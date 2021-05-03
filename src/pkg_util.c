#include <assert.h>
#include <dirent.h>
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
#include "mesg.h"
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

bool check_installed(const struct slack_pkg_dbi *slack_pkg_dbi, const char *pkg_name, struct pkg_options options)
{
        if (options.check_installed) {
                const char *check_tag =
			(options.check_installed & PKG_CHECK_ANY_INSTALLED ? NULL : user_config.sbo_tag);
                return slack_pkg_dbi->is_installed(pkg_name, check_tag);
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

int find_all_meta_pkgs(pkg_nodes_t *meta_pkgs)
{
        struct dirent *dirent = NULL;

        DIR *dp = opendir(user_config.depdir);
        if (dp == NULL)
                return 1;

        while ((dirent = readdir(dp)) != NULL) {
                if (dirent->d_type == DT_DIR)
                        continue;

                if (NULL == pkg_nodes_bsearch_const(meta_pkgs, dirent->d_name)) {
                        if (is_meta_pkg(dirent->d_name)) {
                                struct pkg_node *meta_node = pkg_node_alloc(dirent->d_name);
                                meta_node->pkg.dep.is_meta = true;
                                pkg_nodes_insert_sort(meta_pkgs, meta_node);
                        }
                }
        }

        return 0;
}

int __load_dep(struct pkg_graph *pkg_graph, struct pkg_node *pkg_node, struct pkg_options options,
               pkg_nodes_t *visit_list, struct bds_stack *visit_path)
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
        // pkg_node->color = COLOR_GREY;
        pkg_nodes_insert_sort(visit_list, pkg_node);
        bds_stack_push(visit_path, &pkg_node);

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
                        // if (skip_installed(line, options))
                        //        break;

                        struct pkg_node *req_node = pkg_graph_search(pkg_graph, line);

                        if (req_node == NULL) {
                                mesg_warn("%s no longer in repository but included by %s\n", line,
                                          pkg_node->pkg.name);
                                break;
                        }

                        if (bds_stack_lsearch(visit_path, &req_node, pkg_nodes_compar)) {
                                mesg_error("cyclic dependency found: %s <--> %s\n", pkg_node->pkg.name,
                                           req_node->pkg.name);
                                exit(EXIT_FAILURE);
                        }

                        if (options.revdeps)
                                pkg_insert_parent(&req_node->pkg, pkg_node);

                        pkg_insert_required(&pkg_node->pkg, req_node);

                        /* Avoid revisiting nodes more than once */
                        if (pkg_nodes_bsearch_const(visit_list, req_node->pkg.name) == NULL) {
                                __load_dep(pkg_graph, req_node, options, visit_list, visit_path);
                        }

                } break;
                case BUILDOPTS_BLOCK: {
                        char *buildopt = bds_string_atrim(line);
                        pkg_append_buildopts(&pkg_node->pkg, buildopt);
                } break;
                default:
                        mesg_error("%s(%d): badly formatted dependency file %s\n", __FILE__, __LINE__, dep_file);
                        exit(EXIT_FAILURE);
                }

        cycle:
                free(line);
                line     = NULL;
                num_line = 0;
        }

        struct pkg_node *last_node;
finish:
        // pkg_node->color = COLOR_BLACK;
        assert(bds_stack_pop(visit_path, &last_node) && pkg_node == last_node);

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

        struct bds_stack *visit_path = bds_stack_alloc(1, sizeof(struct pkg_node *), NULL);
        pkg_nodes_t *visit_list      = pkg_nodes_alloc_reference();

        int rc = __load_dep(pkg_graph, pkg_node, options, visit_list, visit_path);

        bds_stack_free(&visit_path);
        pkg_nodes_free(&visit_list);

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
                mesg_info("created %s\n", dep_file);
        } else {
                mesg_error("unable to create %s dependency file\n", pkg->name);
        }
        return dep_file;
}

static void write_buildopts(struct ostream *os, const struct pkg *pkg)
{
        const size_t n = pkg_buildopts_size(pkg);

        if (0 < n) {
                ostream_printf(os, " |");
        }
        for (size_t i = 0; i < n; ++i) {
                ostream_printf(os, " %s", pkg_buildopts_get_const(pkg, i));
        }
}

/*
  Return:
    0    always
 */
static int check_track_pkg(struct pkg *pkg, int node_dist, enum pkg_track_mode track_mode, bool *db_dirty)
{
        if ((PKG_TRACK_ENABLE == track_mode && 0 == node_dist) || PKG_TRACK_ENABLE_ALL == track_mode) {
                pkg->is_tracked = true;
                *db_dirty       = true;
        }
        return 0;
}

/*
 * Return:
 *   -1   if error occurred
 *    0   success / no errors
 *    >0  dep file has been modified, during review (1 == PKG_DEP_REVERTED_DEFAULT, 2 == PKG_DEP_EDITED)
 */

int check_reviewed_pkg(struct pkg *pkg, enum pkg_review_type review_type, bool *db_dirty)
{
        int rc = 0;

        if (review_type == PKG_REVIEW_DISABLED)
                return 0;

        if (pkg->is_reviewed)
                return 0;

        int rc_review = -1;
        switch (review_type) {
        case PKG_REVIEW_DISABLED:
                mesg_error("internal error: review type should not be PKG_REVIEW_DISABLED\n");
                abort();
                break;
        case PKG_REVIEW_AUTO:
                rc_review = 0; /* Set the add-to-REVIEWED status and proceed */
                break;
        case PKG_REVIEW_AUTO_VERBOSE:
                rc_review = pkg_review(pkg);
                break;
        default: // PKG_REVIEW_ENABLED
                /* Use the dep status as the return code */
                rc_review = pkg_review_prompt(pkg, PKG_DEP_REVERTED_DEFAULT, &rc);
                if (rc_review < 0) { /* If an error occurs set error return code */
                        rc = -1;
                }
                break;
        }

        if (rc_review == 0) { // Add-to-REVIEWED status
                pkg->is_reviewed = true;
                *db_dirty        = true;
        }

        return rc;
}

/*
 * Return:
 *   -1   if error occurred
 *    0   success / no errors
 *    >0  dep file has been modified, during review (1 == PKG_DEP_REVERTED_DEFAULT, 2 == PKG_DEP_EDITED)
 */
static int __write_sqf(struct pkg_graph *pkg_graph, const struct slack_pkg_dbi *slack_pkg_dbi,
                       const char *pkg_name, struct pkg_options options, bool *db_dirty,
                       pkg_nodes_t *review_skip_pkgs, pkg_nodes_t *output_pkgs)
{
        int rc = 0;
        struct pkg_iterator iter;

        pkg_iterator_flags_t flags = 0;
        int max_dist               = (options.max_dist >= 0 ? options.max_dist : (options.deep ? -1 : 1));

        if (options.revdeps) {
                flags = PKG_ITER_REVDEPS;
        } else {
                flags = PKG_ITER_DEPS;
        }

        for (struct pkg_node *node = pkg_iterator_begin(&iter, pkg_graph, pkg_name, flags, max_dist); node != NULL;
             node                  = pkg_iterator_next(&iter)) {

                if (node->pkg.dep.is_meta)
                        continue;

                check_track_pkg(&node->pkg, node->dist, options.track_mode, db_dirty);

                if (options.check_installed && strcmp(pkg_name, node->pkg.name) != 0) {
                        const char *tag =
                            (options.check_installed & PKG_CHECK_ANY_INSTALLED ? NULL : user_config.sbo_tag);
                        if (slack_pkg_dbi->is_installed(node->pkg.name, tag)) {
                                // mesg_info("package %s is already installed: skipping\n", node->pkg.name);
                                continue;
                        }
                }

                if (pkg_nodes_bsearch_const(review_skip_pkgs, node->pkg.name) == NULL) {
                        rc = check_reviewed_pkg(&node->pkg, options.review_type, db_dirty);
                        if (rc < 0) {
                                goto finish;
                        }
                        if (rc > 0) {
                                // Package dependency file was edited (it may have been updated): reload the dep
                                // file and process the node
                                pkg_clear_required(&node->pkg);
                                pkg_load_dep(pkg_graph, node->pkg.name, options);
                                goto finish;
                        }
                        pkg_nodes_insert_sort(review_skip_pkgs, node);
                }

                if (pkg_nodes_lsearch_const(output_pkgs, node->pkg.name) == NULL) {
                        pkg_nodes_append(output_pkgs, node);
                }
        }

finish:
        pkg_iterator_destroy(&iter);

        return rc;
}

/*
 * Return:
 *   -1   if error occurred
 *    0   success / no errors
 */
int write_sqf(struct ostream *os, const struct slack_pkg_dbi *slack_pkg_dbi, struct pkg_graph *pkg_graph,
              const string_list_t *pkg_names, struct pkg_options options, bool *db_dirty)
{
        int rc = 0;

        pkg_nodes_t *output_pkgs        = NULL;
        string_list_t *review_skip_pkgs = string_list_alloc_reference();
        const size_t num_pkgs           = string_list_size(pkg_names);

        /* if (options.revdeps) { */
        /*         revdeps_pkgs = bds_stack_alloc(1, sizeof(struct pkg), NULL); */
        /* } */
        output_pkgs = pkg_nodes_alloc_reference();

        for (size_t i = 0; i < num_pkgs; ++i) {
                rc = 0;

                while (1) {
                        rc = __write_sqf(pkg_graph, slack_pkg_dbi,
                                         string_list_get_const(pkg_names, i) /* pkg_name */, options, db_dirty,
                                         review_skip_pkgs, output_pkgs);

                        if (rc > 0) {
                                /* A dependency file was modified during review */
                                continue;
                        }

                        if (rc < 0) { /* Error occurred */
                                goto finish;
                        }
                        break;
                }
        }

        const size_t num_output = pkg_nodes_size(output_pkgs);
        bool have_output        = (num_output > 0);

        for (size_t i = 0; i < num_output; ++i) {
                const struct pkg_node *node = NULL;

                if (options.revdeps) {
                        node = pkg_nodes_get_const(output_pkgs, num_output - 1 - i);
                } else {
                        node = pkg_nodes_get_const(output_pkgs, i);
                }

                ostream_printf(os, "%s", pkg_output_name(options.output_mode, node->pkg.name));
                if (ostream_is_console_stream(os)) {
                        ostream_printf(os, " ");
                } else {
                        write_buildopts(os, &node->pkg);
                        ostream_printf(os, "\n");
                }
        }

        if (have_output && ostream_is_console_stream(os))
                ostream_printf(os, "\n");

finish:
        if (review_skip_pkgs) {
                string_list_free(&review_skip_pkgs);
        }
        if (output_pkgs) {
                pkg_nodes_free(&output_pkgs);
        }

        return rc;
}

int compar_versions(const char *ver_a, const char *ver_b) { return filevercmp(ver_a, ver_b); }

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
                if (WIFEXITED(wstatus)) {          // Child exited normally
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
