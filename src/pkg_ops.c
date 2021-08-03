#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <libbds/bds_queue.h>

#include "file_mmap.h"
#include "mesg.h"
#include "pkg_io.h"
#include "pkg_ops.h"
#include "pkg_util.h"
#include "sbo.h"
#include "user_config.h"
#include "xlimits.h"

#define BORDER1 "================================================================================"
#define BORDER2 "::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::"
#define BORDER3 "--------------------------------------------------------------------------------"

int pkg_create_default_deps(pkg_nodes_t *pkgs)
{
        // printf("creating default dependency files...\n");
        for (size_t i = 0; i < bds_vector_size(pkgs); ++i) {
                struct pkg_node *pkg_node = *(struct pkg_node **)bds_vector_get(pkgs, i);

                if (pkg_dep_file_exists(pkg_node->pkg.name))
                        continue;

                const char *dep_file = NULL;
                if ((dep_file = create_default_dep_verbose(&pkg_node->pkg)) == NULL) {
                        mesg_error("unable to create %s dependency file\n", pkg_node->pkg.name);
                }
        }
        return 0;
}

int pkg_compare_sets(pkg_nodes_t *new_pkgs, pkg_nodes_t *old_pkgs)
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
                struct pkg_node *new_pkg = pkg_nodes_get(new_pkgs, i);
                struct pkg_node *old_pkg = pkg_nodes_bsearch(old_pkgs, new_pkg->pkg.name);

                if (old_pkg) {
                        // Preserve tracked flag
                        new_pkg->pkg.is_tracked = old_pkg->pkg.is_tracked;
                        if (old_pkg->pkg.info_crc == new_pkg->pkg.info_crc) {
                                /*
                                  If there is no change in the package, preserved the reviewed flag; otherwise it
                                  will be set to false (0).
                                */
                                new_pkg->pkg.is_reviewed = old_pkg->pkg.is_reviewed;
                        } else {
                                int ver_diff = pkg_compare_versions(old_pkg->pkg.version, new_pkg->pkg.version);
                                struct pkg updated_pkg[2] = {old_pkg->pkg, new_pkg->pkg};

                                if (ver_diff == 0) {
                                        bds_queue_push(modified_pkg_queue, &updated_pkg[0]);
                                } else if (ver_diff < 0) {
                                        bds_queue_push(upgraded_pkg_queue, &updated_pkg);
                                } else {
                                        bds_queue_push(downgraded_pkg_queue, &updated_pkg);
                                }
                        }
                        // TODO: check if this is needed
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
                        assert((dep = file_mmap(file_name)) != NULL);
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
        int        rc    = 0;
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
                        if (0 != pkg_edit_dep(pkg->name))
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

static const char *find_dep_file(const char *pkg_name)
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

int pkg_edit_dep(const char *pkg_name)
{
        int              rc = 0;
        pid_t            cpid;
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
                char *       args[num_args];

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
                int   wstatus = 0;

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
