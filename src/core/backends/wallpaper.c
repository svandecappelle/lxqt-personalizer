/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Desktop wallpaper drawn by pcmanfm-qt:
 * ~/.config/pcmanfm-qt/<profile>/settings.conf [Desktop].
 * The running desktop keeps its settings in memory and saves them back,
 * so reload() hands the new wallpaper to it on the command line.
 *
 * In a theme, a relative "file" is relative to the theme directory.
 */
#include "backends.h"

#include "lxp-error.h"
#include "lxp-ini.h"
#include "lxp-paths.h"
#include "lxp-system.h"

#define GROUP           "Wallpaper"
#define DEFAULT_PROFILE "lxqt"

static const char *const modes[] = {
    "color", "stretch", "fit", "center", "tile", "zoom", NULL,
};

/* Profile of the running desktop, e.g. "pcmanfm-qt --desktop --profile=lxqt". */
static char *current_profile(void)
{
    g_auto(GStrv) argv = lxp_process_find("pcmanfm-qt");
    char *profile = argv ? lxp_argv_option((const char *const *)argv, "profile") : NULL;

    return profile ? profile : g_strdup(DEFAULT_PROFILE);
}

static char *settings_path(const char *profile)
{
    g_autofree char *rel = g_build_filename("pcmanfm-qt", profile, "settings.conf", NULL);
    return lxp_paths_user_config(rel);
}

static char *theme_wallpaper(const LxpTheme *theme)
{
    g_autofree char *file = lxp_theme_get_string(theme, GROUP, "file");
    const char *dir = lxp_theme_get_dir(theme);

    if (!file || g_path_is_absolute(file) || !dir)
        return g_steal_pointer(&file);
    return g_build_filename(dir, file, NULL);
}

static gboolean wallpaper_available(void)
{
    g_autofree char *bin = g_find_program_in_path("pcmanfm-qt");
    return bin != NULL;
}

static GPtrArray *wallpaper_managed_files(void)
{
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);
    g_autofree char *profile = current_profile();

    g_ptr_array_add(files, settings_path(profile));
    return files;
}

static gboolean wallpaper_check(const LxpTheme *theme, GError **error)
{
    g_autofree char *file = theme_wallpaper(theme);
    g_autofree char *mode = lxp_theme_get_string(theme, GROUP, "mode");

    if (file && !g_file_test(file, G_FILE_TEST_IS_REGULAR)) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_NOT_FOUND,
                    "Wallpaper “%s” not found", file);
        return FALSE;
    }
    if (mode && !g_strv_contains(modes, mode)) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_INVALID,
                    "Unknown wallpaper mode “%s”", mode);
        return FALSE;
    }
    return TRUE;
}

static gboolean wallpaper_capture(LxpTheme *theme, GError **error)
{
    g_autofree char *profile = current_profile();
    g_autofree char *path = settings_path(profile);
    g_autoptr(LxpIni) ini = lxp_ini_load(path, TRUE, error);
    const char *raw, *mode;

    if (!ini)
        return FALSE;

    lxp_theme_remove_group(theme, GROUP);
    raw = lxp_ini_get(ini, "Desktop", "Wallpaper");
    if (raw && *raw) {
        g_autofree char *file = lxp_qt_unquote(raw);
        lxp_theme_set_string(theme, GROUP, "file", file);
    }
    mode = lxp_ini_get(ini, "Desktop", "WallpaperMode");
    if (mode)
        lxp_theme_set_string(theme, GROUP, "mode", mode);
    return TRUE;
}

static gboolean wallpaper_apply(const LxpTheme *theme, GError **error)
{
    g_autofree char *file = theme_wallpaper(theme);
    g_autofree char *mode = lxp_theme_get_string(theme, GROUP, "mode");
    g_autofree char *profile = NULL;
    g_autofree char *path = NULL;
    g_autoptr(LxpIni) ini = NULL;

    if (!file && !mode)
        return TRUE;

    profile = current_profile();
    path = settings_path(profile);
    if (!(ini = lxp_ini_load(path, TRUE, error)))
        return FALSE;

    if (file) {
        g_autofree char *quoted = lxp_qt_quote(file);
        lxp_ini_set(ini, "Desktop", "Wallpaper", quoted);
    }
    if (mode)
        lxp_ini_set(ini, "Desktop", "WallpaperMode", mode);
    return lxp_ini_save(ini, path, error);
}

static gboolean wallpaper_reload(GError **error)
{
    g_auto(GStrv) running = lxp_process_find("pcmanfm-qt");
    g_autofree char *profile = NULL;
    g_autofree char *path = NULL;
    g_autofree char *file = NULL;
    g_autoptr(LxpIni) ini = NULL;
    const char *raw, *mode;

    if (!running || !g_strv_contains((const char *const *)running, "--desktop"))
        return TRUE;

    profile = current_profile();
    path = settings_path(profile);
    if (!(ini = lxp_ini_load(path, TRUE, error)))
        return FALSE;
    raw = lxp_ini_get(ini, "Desktop", "Wallpaper");
    mode = lxp_ini_get(ini, "Desktop", "WallpaperMode");
    if (!raw || !*raw)
        return TRUE;
    file = lxp_qt_unquote(raw);

    {
        g_autofree char *profile_arg = g_strconcat("--profile=", profile, NULL);
        g_autofree char *file_arg = g_strconcat("--set-wallpaper=", file, NULL);
        g_autofree char *mode_arg = mode ? g_strconcat("--wallpaper-mode=", mode, NULL) : NULL;
        const char *argv[] = { "pcmanfm-qt", profile_arg, file_arg, mode_arg, NULL };

        return lxp_run(argv, error);
    }
}

const LxpBackend lxp_backend_wallpaper = {
    .id = "wallpaper",
    .description = "Desktop wallpaper (pcmanfm-qt)",
    .available = wallpaper_available,
    .managed_files = wallpaper_managed_files,
    .check = wallpaper_check,
    .capture = wallpaper_capture,
    .apply = wallpaper_apply,
    .reload = wallpaper_reload,
};
