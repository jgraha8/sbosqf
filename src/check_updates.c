void print_check_updates_help()
{
        printf("Usage: %s check-updates [option] [pkg]\n"
               "Checks for updated packages.\n"
               "\n"
               "If a package name is not provided, then updates for all packages will be provided.\n"
               "\n"
               "Options:\n"
               "  -h, --help\n"
               "  -R, --repo-db\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

int process_check_updates_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *        options_str    = "hR";
        static const struct option long_options[] = {
            LONG_OPT("help", 'h'), LONG_OPT("repo-db", 'R'), {0, 0, 0, 0}};

        return process_options(argc, argv, options_str, long_options, command_check_updates_help, options);
}

enum updated_pkg_status {
        PKG_CHANGE_NONE = 0,
        PKG_UPDATED,
        PKG_DOWNGRADED,
        PKG_REMOVED,
};

struct updated_pkg {
        enum updated_pkg_status status;
        const struct pkg_node * node;
        char *                  name;
        char *                  slack_pkg_version;
        char *                  sbo_version;
};

static void destroy_updated_pkg(struct updated_pkg *updated_pkg)
{
        if (updated_pkg->name)
                free(updated_pkg->name);
        if (updated_pkg->slack_pkg_version)
                free(updated_pkg->slack_pkg_version);
        if (updated_pkg->sbo_version)
                free(updated_pkg->sbo_version);

        memset(updated_pkg, 0, sizeof(*updated_pkg));
}

static int get_updated_pkgs(const struct slack_pkg_dbi *slack_pkg_dbi,
                            struct pkg_graph *          pkg_graph,
                            const char *                pkg_name,
                            struct bds_queue *          updated_pkg_queue)
{
        bool         have_pkg  = false; /* Used only for single package pkg_name */
        pkg_nodes_t *pkg_nodes = NULL;

        if (pkg_name) {
                pkg_nodes             = pkg_nodes_alloc_reference();
                struct pkg_node *node = pkg_graph_search(pkg_graph, pkg_name);
                if (node == NULL)
                        goto finish;

                pkg_nodes_append(pkg_nodes, node);
        } else {
                pkg_nodes = pkg_graph_sbo_pkgs(pkg_graph);
        }
        const ssize_t num_slack_pkgs = slack_pkg_dbi->size();
        if (num_slack_pkgs < 0)
                return 1;

        for (ssize_t i = 0; i < num_slack_pkgs; ++i) {

                struct updated_pkg updated_pkg;

                const struct slack_pkg *slack_pkg = slack_pkg_dbi->get_const((size_t)i, user_config.sbo_tag);
                if (slack_pkg == NULL)
                        continue; /* tag did not match */

                const struct pkg_node *node = pkg_nodes_bsearch_const(pkg_nodes, slack_pkg->name);
                if (node == NULL) {
                        if (pkg_name == NULL) { /* the package has been removed */
                                updated_pkg.status            = PKG_REMOVED;
                                updated_pkg.node              = NULL;
                                updated_pkg.name              = bds_string_dup(slack_pkg->name);
                                updated_pkg.slack_pkg_version = bds_string_dup(slack_pkg->version);
                                updated_pkg.sbo_version       = NULL;
                                bds_queue_push(updated_pkg_queue, &updated_pkg);
                        }
                        continue;
                }

                if (pkg_name)
                        have_pkg = true;

                const char *sbo_version = sbo_read_version(node->pkg.sbo_dir, node->pkg.name);
                assert(sbo_version);

                int diff = compar_versions(slack_pkg->version, sbo_version);
                if (diff == 0)
                        continue;

                updated_pkg.status            = (diff < 0 ? PKG_UPDATED : PKG_DOWNGRADED);
                updated_pkg.node              = node;
                updated_pkg.name              = bds_string_dup(node->pkg.name);
                updated_pkg.slack_pkg_version = bds_string_dup(slack_pkg->version);
                updated_pkg.sbo_version       = bds_string_dup(sbo_version);
                bds_queue_push(updated_pkg_queue, &updated_pkg);
        }
finish:
        if (pkg_name) {
                if (!have_pkg) {
                        /* Check if it's installed/present */
                        const struct slack_pkg *slack_pkg =
                            slack_pkg_dbi->search_const(pkg_name, user_config.sbo_tag);
                        if (slack_pkg) {
                                struct updated_pkg updated_pkg;
                                updated_pkg.status            = PKG_REMOVED;
                                updated_pkg.node              = NULL;
                                updated_pkg.name              = bds_string_dup(slack_pkg->name);
                                updated_pkg.slack_pkg_version = bds_string_dup(slack_pkg->version);
                                updated_pkg.sbo_version       = NULL;
                                bds_queue_push(updated_pkg_queue, &updated_pkg);
                        }
                }
                pkg_nodes_free(&pkg_nodes);
        }
        return 0;
}

int run_check_updates_command(const struct slack_pkg_dbi *slack_pkg_dbi,
                              struct pkg_graph *          pkg_graph,
                              const char *                pkg_name)
{
        int rc = 0;

        struct bds_queue *updated_pkg_queue =
            bds_queue_alloc(1, sizeof(struct updated_pkg), (void (*)(void *))destroy_updated_pkg);

        bds_queue_set_autoresize(updated_pkg_queue, true);

        if ((rc = get_updated_pkgs(slack_pkg_dbi, pkg_graph, pkg_name, updated_pkg_queue)) != 0)
                goto finish;

        struct updated_pkg updated_pkg;

        while (bds_queue_pop(updated_pkg_queue, &updated_pkg)) {
                switch (updated_pkg.status) {
                case PKG_UPDATED:
                        mesg_ok_label("%4s", " %-24s %-8s --> %s\n", "[U]", updated_pkg.name,
                                      updated_pkg.slack_pkg_version, updated_pkg.sbo_version);
                        break;
                case PKG_DOWNGRADED:
                        mesg_info_label("%4s", " %-24s %-8s --> %s\n", "[D]", updated_pkg.name,
                                        updated_pkg.slack_pkg_version, updated_pkg.sbo_version);
                        break;
                default: /* PKG_REMOVED */
                        mesg_error_label("%4s", " %-24s %-8s\n", "[R]", updated_pkg.name,
                                         updated_pkg.slack_pkg_version);
                }
                destroy_updated_pkg(&updated_pkg);
        }

finish:
        if (updated_pkg_queue)
                bds_queue_free(&updated_pkg_queue);

        return rc;
}
