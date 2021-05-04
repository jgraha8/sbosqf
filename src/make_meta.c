void print_make_meta_help()
{
        printf("Usage: %s make-meta -o metapkg [options] pkgs...\n"
               "Creates a meta package from a set of provided packages.\n"
               "\n"
               "NOTE: Option -o is required.\n"
               "\n"
               "Options:\n"
               "  -o, --output metapkg\n"
               "  -h, --help\n",
               "sbopkg-dep2sqf"); // TODO: have program_name variable
}

int process_make_meta_options(int argc, char **argv, struct pkg_options *options)
{
        static const char *        options_str    = "o:h";
        static const struct option long_options[] = {LONG_OPT("output", 'o'), LONG_OPT("help", 'h'), {0, 0, 0, 0}};

        int rc = process_options(argc, argv, options_str, long_options, command_make_meta_help, options);

        if (rc == 0) {
                if (options->output_name == NULL) {
                        mesg_error("Output metapkg not provided (option --output/-o\n");
                        rc = 1;
                }
        }

        /* Check that the meta package provided does not already exist as a package in the repo */

        return rc;
}

int make_meta_pkg(const pkg_nodes_t *sbo_pkgs, const char *meta_pkg_name, string_list_t *pkg_names)
{
        char         meta_pkg_path[4096];
        const size_t num_pkgs = string_list_size(pkg_names);

        if (0 == num_pkgs) {
                mesg_warn("no packages provided for meta package %s\n", meta_pkg_name);
                return 1;
        }

        // Check if the meta package conflicts with an existing package
        if (pkg_nodes_bsearch_const(sbo_pkgs, meta_pkg_name)) {
                mesg_error("meta-package %s conflict with existing %s package\n", meta_pkg_name, meta_pkg_name);
                return 1;
        }

        bds_string_copyf(meta_pkg_path, sizeof(meta_pkg_path), "%s/%s", user_config.depdir, meta_pkg_name);

        FILE *fp = fopen(meta_pkg_path, "w");
        assert(fp);

        fprintf(fp, "METAPKG\n"
                    "REQUIRED:\n");
        for (size_t i = 0; i < num_pkgs; ++i) {
                fprintf(fp, "%s\n", string_list_get_const(pkg_names, i));
        }
        fclose(fp);

        return 0;
}
