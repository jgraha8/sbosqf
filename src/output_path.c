const char *get_output_path(struct pkg_options pkg_options, const string_list_t *pkg_names)
{
        static char buf[256]    = {0};
        const char *output_path = "/dev/stdout";

        if (pkg_options.output_mode == PKG_OUTPUT_FILE) {
                if (string_list_size(pkg_names) > 1)
                        assert(pkg_options.output_name);

                if (pkg_options.output_name) {
                        bds_string_copyf(buf, sizeof(buf), "%s", pkg_options.output_name);
                } else {
                        bds_string_copyf(buf, sizeof(buf), "%s.sqf", string_list_get_const(pkg_names, 0));
                }
                output_path = buf;
        }
        return output_path;
}
