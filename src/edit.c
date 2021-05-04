void print_edit_help()
{
        printf("Usage: %s edit [option] pkg\n"
               "Options:\n"
               "  -h, --help\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

int process_edit_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *        options_str    = "h";
        static const struct option long_options[] = {LONG_OPT("help", 'h'), {0, 0, 0, 0}};

        return process_options(argc, argv, options_str, long_options, command_edit_help, options);
}


static void sigchld_handler(int signo) { fprintf(stderr, "caught signal %d\n", signo); }

static int edit_dep(const char *pkg_name)
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

int run_edit_command(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        int              rc       = 0;
        struct pkg_node *pkg_node = NULL;

        pkg_node = (struct pkg_node *)pkg_graph_search(pkg_graph, pkg_name);
        if (pkg_node == NULL) {
                mesg_error("package %s does not exist\n", pkg_name);
                return 1;
        }

        rc = edit_dep(pkg_node->pkg.name);
        if (rc != 0)
                return rc;

        /*
          Mark not reviewed
         */
        pkg_node->pkg.is_reviewed = false;

        return pkg_write_db(pkg_graph_sbo_pkgs(pkg_graph));
}
