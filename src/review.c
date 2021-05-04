static void print_review_help()
{
        printf("Usage: %s review [option] pkg\n"
               "Options:\n"
               "  -h, --help\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

static int process_review_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *        options_str    = "h";
        static const struct option long_options[] = {LONG_OPT("help", 'h'), {0, 0, 0, 0}};

        return process_options(argc, argv, options_str, long_options, command_review_help, options);
}

static int run_review_command(struct pkg_graph *pkg_graph, const char *pkg_name)
{
        int  rc       = 0;
        bool db_dirty = false;

        struct pkg_node *  pkg_node    = NULL;
        struct pkg_options pkg_options = pkg_options_default();

        pkg_options.recursive = false;

        rc = pkg_load_dep(pkg_graph, pkg_name, pkg_options);
        if (rc != 0)
                return rc;

        pkg_node = pkg_graph_search(pkg_graph, pkg_name);
        if (pkg_node == NULL) {
                mesg_error("package %s does not exist\n", pkg_name);
                rc = 1;
                return rc;
        }

        if (pkg_node->pkg.is_reviewed) {
                rc = pkg_review(&pkg_node->pkg);
        } else {
                int dep_status;
                rc = pkg_review_prompt(&pkg_node->pkg, 0, &dep_status);
                if (rc == 0) {
                        pkg_node->pkg.is_reviewed = true;
                        db_dirty                  = true;
                }
        }

        if (db_dirty)
                pkg_write_db(pkg_graph_sbo_pkgs(pkg_graph));

        return rc;
}
