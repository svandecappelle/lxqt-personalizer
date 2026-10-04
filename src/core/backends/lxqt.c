/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LXQt appearance: ~/.config/lxqt/lxqt.conf (QSettings INI format).
 * LXQt layers this file over $XDG_CONFIG_DIRS and /usr/share/lxqt, and
 * every LXQt/Qt app watches it, so no explicit reload is needed.
 */
#include "backends.h"

#include "lxp-ini.h"
#include "lxp-paths.h"
#include "lxp-system.h"

#define LXQT_CONF        "lxqt/lxqt.conf"
#define GROUP            "LXQt"
#define GROUP_PALETTE    "LXQt Palette"

static const struct {
    const char *group;      /* in lxqt.conf */
    const char *key;        /* in lxqt.conf */
    const char *theme_key;  /* in [LXQt] of theme.ini */
} keys[] = {
    { "General", "theme",                    "theme" },
    { "General", "icon_theme",               "icon_theme" },
    { "General", "icon_follow_color_scheme", "icon_follow_color_scheme" },
    { "General", "tool_button_style",        "tool_button_style" },
    { "Qt",      "style",                    "style" },
    { "Qt",      "font",                     "font" },
};

/* Layers of lxqt.conf, lowest priority first. */
static GPtrArray *load_layers(void)
{
    g_autoptr(GPtrArray) paths = lxp_paths_find_config_layers(LXQT_CONF);
    GPtrArray *layers = g_ptr_array_new_with_free_func((GDestroyNotify)lxp_ini_free);

    for (guint i = 0; i < paths->len; i++) {
        const char *path = g_ptr_array_index(paths, i);
        g_autoptr(GError) error = NULL;
        LxpIni *ini = lxp_ini_load(path, FALSE, &error);

        if (ini)
            g_ptr_array_add(layers, ini);
        else
            g_warning("Skipping %s: %s", path, error->message);
    }
    return layers;
}

static gboolean lxqt_available(void)
{
    g_autofree char *conf = lxp_paths_find_config(LXQT_CONF);
    g_autofree char *session = g_find_program_in_path("lxqt-session");

    return conf || session;
}

static GPtrArray *lxqt_managed_files(void)
{
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);

    g_ptr_array_add(files, lxp_paths_user_config(LXQT_CONF));
    return files;
}

static gboolean lxqt_check(const LxpTheme *theme, GError **error)
{
    g_autofree char *name = lxp_theme_get_string(theme, GROUP, "theme");
    g_autofree char *icons = lxp_theme_get_string(theme, GROUP, "icon_theme");

    if (name && !lxp_backend_require(lxp_find_installed("lxqt/themes", name, NULL),
                                     "LXQt theme", name, error))
        return FALSE;
    if (icons && !lxp_backend_require(lxp_find_installed("icons", icons, "index.theme"),
                                      "Icon theme", icons, error))
        return FALSE;
    return TRUE;
}

static gboolean lxqt_capture(LxpTheme *theme, GError **error)
{
    g_autoptr(GPtrArray) layers = load_layers();

    lxp_theme_remove_group(theme, GROUP);
    lxp_theme_remove_group(theme, GROUP_PALETTE);

    for (gsize k = 0; k < G_N_ELEMENTS(keys); k++) {
        for (guint i = layers->len; i > 0; i--) {
            const char *raw = lxp_ini_get(g_ptr_array_index(layers, i - 1),
                                          keys[k].group, keys[k].key);
            if (raw) {
                g_autofree char *value = lxp_qt_unquote(raw);
                lxp_theme_set_string(theme, GROUP, keys[k].theme_key, value);
                break;
            }
        }
    }

    /* A custom palette is all-or-nothing: take the highest layer that has one. */
    for (guint i = layers->len; i > 0; i--) {
        LxpIni *ini = g_ptr_array_index(layers, i - 1);
        g_auto(GStrv) names = lxp_ini_get_keys(ini, "Palette");

        if (!names[0])
            continue;
        for (char **n = names; *n; n++)
            lxp_theme_set_string(theme, GROUP_PALETTE, *n,
                                 lxp_ini_get(ini, "Palette", *n));
        break;
    }
    return TRUE;
}

static gboolean lxqt_apply(const LxpTheme *theme, GError **error)
{
    GKeyFile *kf = lxp_theme_get_keyfile(theme);
    g_autofree char *path = lxp_paths_user_config(LXQT_CONF);
    g_autoptr(LxpIni) ini = NULL;
    g_auto(GStrv) palette = NULL;
    g_autofree char *stamp = NULL;

    if (!g_key_file_has_group(kf, GROUP))
        return TRUE;
    if (!(ini = lxp_ini_load(path, TRUE, error)))
        return FALSE;

    for (gsize k = 0; k < G_N_ELEMENTS(keys); k++) {
        g_autofree char *value = lxp_theme_get_string(theme, GROUP, keys[k].theme_key);
        g_autofree char *quoted = NULL;

        if (!value)
            continue;
        quoted = lxp_qt_quote(value);
        lxp_ini_set(ini, keys[k].group, keys[k].key, quoted);
    }

    /*
     * The palette is replaced as a whole, in place. No palette in the
     * theme means "use the LXQt theme's own colors".
     */
    palette = g_key_file_get_keys(kf, GROUP_PALETTE, NULL, NULL);
    if (!palette) {
        lxp_ini_remove_group(ini, "Palette");
    } else {
        g_auto(GStrv) current = lxp_ini_get_keys(ini, "Palette");

        for (char **n = current; *n; n++)
            if (!g_strv_contains((const char *const *)palette, *n))
                lxp_ini_remove(ini, "Palette", *n);
        for (char **n = palette; *n; n++) {
            g_autofree char *value = lxp_theme_get_string(theme, GROUP_PALETTE, *n);
            lxp_ini_set(ini, "Palette", *n, value);
        }
    }

    /* Tells running LXQt components to reload the theme files too. */
    stamp = g_strdup_printf("%" G_GINT64_FORMAT, g_get_real_time() / 1000);
    lxp_ini_set(ini, "General", "__theme_updated__", stamp);

    return lxp_ini_save(ini, path, error);
}

const LxpBackend lxp_backend_lxqt = {
    .id = "lxqt",
    .description = "LXQt theme, icons, Qt style, palette and font",
    .available = lxqt_available,
    .managed_files = lxqt_managed_files,
    .check = lxqt_check,
    .capture = lxqt_capture,
    .apply = lxqt_apply,
};
