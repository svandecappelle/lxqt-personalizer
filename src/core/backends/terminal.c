/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QTerminal colors: [General] colorScheme in ~/.config/qterminal.org/
 * qterminal.ini, naming a qtermwidget color scheme. Generated schemes
 * ("lxp-*") come from the theme's data/qtermwidget6/color-schemes and are
 * copied to $XDG_DATA_HOME/qtermwidget6/color-schemes. New terminal windows
 * use the new colors.
 */
#include "backends.h"

#include "lxp-error.h"
#include "lxp-fileutil.h"
#include "lxp-ini.h"
#include "lxp-paths.h"

#include <string.h>

#define GROUP       "Terminal"
#define INI         "qterminal.org/qterminal.ini"
#define SCHEMES_DIR "qtermwidget6/color-schemes"

static char *scheme_file(const char *name)
{
    return g_strconcat(name, ".colorscheme", NULL);
}

static gboolean valid_name(const char *name)
{
    return name && *name && !strchr(name, G_DIR_SEPARATOR) && name[0] != '.';
}

/* The theme's own generated scheme file, or NULL. */
static char *bundled(const LxpTheme *theme, const char *name)
{
    const char *dir = lxp_theme_get_dir(theme);
    g_autofree char *file = scheme_file(name);
    g_autofree char *path = dir ? g_build_filename(dir, "data", SCHEMES_DIR, file, NULL) : NULL;

    return path && g_file_test(path, G_FILE_TEST_IS_REGULAR) ? g_steal_pointer(&path) : NULL;
}

static char *installed(const char *name)
{
    g_autofree char *file = scheme_file(name);
    g_autofree char *user = g_build_filename(g_get_user_data_dir(), SCHEMES_DIR, file, NULL);

    if (g_file_test(user, G_FILE_TEST_IS_REGULAR))
        return g_steal_pointer(&user);
    for (const char *const *d = g_get_system_data_dirs(); *d; d++) {
        char *path = g_build_filename(*d, SCHEMES_DIR, file, NULL);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
            return path;
        g_free(path);
    }
    return NULL;
}

static gboolean terminal_available(void)
{
    g_autofree char *bin = g_find_program_in_path("qterminal");
    return bin != NULL;
}

static GPtrArray *terminal_managed_files(void)
{
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);

    g_ptr_array_add(files, lxp_paths_user_config(INI));
    return files;
}

static gboolean terminal_check(const LxpTheme *theme, GError **error)
{
    g_autofree char *name = lxp_theme_get_string(theme, GROUP, "color_scheme");
    char *found;

    if (!name)
        return TRUE;
    if (!valid_name(name)) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_INVALID, "Invalid color scheme “%s”", name);
        return FALSE;
    }
    found = installed(name);
    if (!found)
        found = bundled(theme, name);
    return lxp_backend_require(found, "Terminal color scheme", name, error);
}

static gboolean terminal_capture(LxpTheme *theme, GError **error)
{
    g_autofree char *path = lxp_paths_user_config(INI);
    g_autoptr(LxpIni) ini = lxp_ini_load(path, TRUE, error);
    const char *name;

    if (!ini)
        return FALSE;
    lxp_theme_remove_group(theme, GROUP);
    if ((name = lxp_ini_get(ini, "General", "colorScheme")) && *name)
        lxp_theme_set_string(theme, GROUP, "color_scheme", name);
    return TRUE;
}

static gboolean terminal_apply(const LxpTheme *theme, GError **error)
{
    g_autofree char *name = lxp_theme_get_string(theme, GROUP, "color_scheme");
    g_autofree char *path = NULL;
    g_autofree char *source = NULL;
    g_autoptr(LxpIni) ini = NULL;

    if (!name)
        return TRUE;
    if (!valid_name(name)) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_INVALID, "Invalid color scheme “%s”", name);
        return FALSE;
    }

    /* our generated schemes travel with the theme */
    if (g_str_has_prefix(name, "lxp-") && (source = bundled(theme, name))) {
        g_autofree char *file = scheme_file(name);
        g_autofree char *dest = g_build_filename(g_get_user_data_dir(), SCHEMES_DIR, file, NULL);
        if (!lxp_copy_atomic(source, dest, 0644, error))
            return FALSE;
    }

    path = lxp_paths_user_config(INI);
    if (!(ini = lxp_ini_load(path, TRUE, error)))
        return FALSE;
    lxp_ini_set(ini, "General", "colorScheme", name);
    return lxp_ini_save(ini, path, error);
}

const LxpBackend lxp_backend_terminal = {
    .id = "terminal",
    .description = "QTerminal colors",
    .available = terminal_available,
    .managed_files = terminal_managed_files,
    .check = terminal_check,
    .capture = terminal_capture,
    .apply = terminal_apply,
};
