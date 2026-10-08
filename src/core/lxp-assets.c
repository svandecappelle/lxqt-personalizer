/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-assets.h"

#include "lxp-error.h"
#include "lxp-fileutil.h"

#include <glib/gstdio.h>
#include <string.h>

#define MAX_DEPTH 4

static gboolean has_marker(const char *dir)
{
    g_autofree char *marker = g_build_filename(dir, LXP_ASSET_MARKER, NULL);
    return g_file_test(marker, G_FILE_TEST_IS_REGULAR);
}

/* Replace <dest> with a copy of <unit> (via a temporary sibling). */
static gboolean install_unit(const char *unit, const char *dest, GError **error)
{
    g_autofree char *tmp = g_strconcat(dest, ".lxp-new", NULL);
    g_autofree char *old = g_strconcat(dest, ".lxp-old", NULL);

    if (g_file_test(dest, G_FILE_TEST_EXISTS) && !has_marker(dest)) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_EXISTS,
                    "%s already exists and was not installed by lxqt-personalizer", dest);
        return FALSE;
    }
    if (!lxp_remove_tree(tmp, error) || !lxp_remove_tree(old, error) ||
        !lxp_copy_tree(unit, tmp, error))
        return FALSE;

    if (g_file_test(dest, G_FILE_TEST_EXISTS) && g_rename(dest, old) != 0) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_FAILED, "Failed to replace %s", dest);
        return FALSE;
    }
    if (g_rename(tmp, dest) != 0) {
        g_rename(old, dest);
        g_set_error(error, LXP_ERROR, LXP_ERROR_FAILED, "Failed to install %s", dest);
        return FALSE;
    }
    return lxp_remove_tree(old, error);
}

static gboolean install_dir(const char *src, const char *dest, int depth, GError **error)
{
    g_autoptr(GDir) dir = NULL;
    const char *name;

    if (has_marker(src))
        return install_unit(src, dest, error);
    if (depth >= MAX_DEPTH || !(dir = g_dir_open(src, 0, NULL)))
        return TRUE;

    while ((name = g_dir_read_name(dir))) {
        g_autofree char *s = g_build_filename(src, name, NULL);
        g_autofree char *d = g_build_filename(dest, name, NULL);

        if (g_file_test(s, G_FILE_TEST_IS_DIR) && !g_file_test(s, G_FILE_TEST_IS_SYMLINK) &&
            !install_dir(s, d, depth + 1, error))
            return FALSE;
    }
    return TRUE;
}

gboolean lxp_assets_install(const LxpTheme *theme, GError **error)
{
    const char *dir = lxp_theme_get_dir(theme);
    g_autofree char *data = dir ? g_build_filename(dir, "data", NULL) : NULL;

    if (!data || !g_file_test(data, G_FILE_TEST_IS_DIR))
        return TRUE;
    return install_dir(data, g_get_user_data_dir(), 0, error);
}

char *lxp_assets_find(const LxpTheme *theme, const char *kind, const char *name,
                      const char *probe)
{
    const char *dir = lxp_theme_get_dir(theme);
    g_autofree char *unit = NULL;
    g_autofree char *path = NULL;

    if (!dir || !name || !*name || strchr(name, G_DIR_SEPARATOR))
        return NULL;
    unit = g_build_filename(dir, "data", kind, name, NULL);
    path = probe ? g_build_filename(unit, probe, NULL) : g_strdup(unit);
    return g_file_test(path, G_FILE_TEST_EXISTS) ? g_steal_pointer(&unit) : NULL;
}
