/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-paths.h"

#include "config.h"

char *lxp_paths_themes_dir(void)
{
    return g_build_filename(g_get_user_data_dir(), LXP_APP_ID, "themes", NULL);
}

char *lxp_paths_snapshots_dir(void)
{
    return g_build_filename(g_get_user_state_dir(), LXP_APP_ID, "snapshots", NULL);
}

char *lxp_paths_user_config(const char *relpath)
{
    return g_build_filename(g_get_user_config_dir(), relpath, NULL);
}

/* Candidate paths, highest priority first. */
static GPtrArray *candidates(const char *relpath)
{
    GPtrArray *list = g_ptr_array_new_with_free_func(g_free);

    g_ptr_array_add(list, lxp_paths_user_config(relpath));
    for (const char *const *d = g_get_system_config_dirs(); *d; d++)
        g_ptr_array_add(list, g_build_filename(*d, relpath, NULL));
    for (const char *const *d = g_get_system_data_dirs(); *d; d++)
        g_ptr_array_add(list, g_build_filename(*d, relpath, NULL));
    return list;
}

char *lxp_paths_find_config(const char *relpath)
{
    g_autoptr(GPtrArray) list = candidates(relpath);

    for (guint i = 0; i < list->len; i++) {
        const char *path = g_ptr_array_index(list, i);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
            return g_strdup(path);
    }
    return NULL;
}

GPtrArray *lxp_paths_find_config_layers(const char *relpath)
{
    g_autoptr(GPtrArray) list = candidates(relpath);
    GPtrArray *found = g_ptr_array_new_with_free_func(g_free);

    for (guint i = list->len; i > 0; i--) {
        const char *path = g_ptr_array_index(list, i - 1);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
            g_ptr_array_add(found, g_strdup(path));
    }
    return found;
}

static void add_dir(GPtrArray *dirs, char *dir)
{
    g_autofree char *canonical = g_canonicalize_filename(dir, NULL);
    g_autofree char *user = lxp_paths_themes_dir();
    g_autofree char *user_canonical = g_canonicalize_filename(user, NULL);

    g_free(dir);
    /* $XDG_DATA_DIRS may list ~/.local/share: the user's themes are not built in */
    if (g_str_equal(canonical, user_canonical) ||
        !g_file_test(canonical, G_FILE_TEST_IS_DIR) ||
        g_ptr_array_find_with_equal_func(dirs, canonical, g_str_equal, NULL))
        return;
    g_ptr_array_add(dirs, g_steal_pointer(&canonical));
}

GPtrArray *lxp_paths_builtin_dirs(void)
{
    GPtrArray *dirs = g_ptr_array_new_with_free_func(g_free);
    const char *env = g_getenv("LXP_BUILTIN_DIR");

    if (env && *env)
        add_dir(dirs, g_strdup(env));
    for (const char *const *d = g_get_system_data_dirs(); *d; d++)
        add_dir(dirs, g_build_filename(*d, LXP_APP_ID, "themes", NULL));
    add_dir(dirs, g_build_filename(LXP_INSTALL_DATADIR, LXP_APP_ID, "themes", NULL));
    return dirs;
}

char *lxp_paths_builtin_cache_dir(void)
{
    return g_build_filename(g_get_user_cache_dir(), LXP_APP_ID, "builtin", NULL);
}
