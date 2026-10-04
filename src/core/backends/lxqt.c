/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LXQt appearance: ~/.config/lxqt/lxqt.conf (QSettings INI format).
 * LXQt layers this file over $XDG_CONFIG_DIRS and /usr/share/lxqt.
 */
#include "backends.h"

#include "lxp-paths.h"

#include <string.h>

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

/* QSettings quotes values containing commas: "Sans,11,-1,5,50,0,0,0,0,0" */
static char *qt_unquote(const char *raw)
{
    size_t len = strlen(raw);
    GString *out;

    if (len < 2 || raw[0] != '"' || raw[len - 1] != '"')
        return g_strdup(raw);

    out = g_string_sized_new(len);
    for (size_t i = 1; i < len - 1; i++) {
        if (raw[i] == '\\' && i + 1 < len - 1)
            i++;
        g_string_append_c(out, raw[i]);
    }
    return g_string_free(out, FALSE);
}

/* Layers of lxqt.conf, lowest priority first. */
static GPtrArray *load_layers(void)
{
    g_autoptr(GPtrArray) paths = lxp_paths_find_config_layers(LXQT_CONF);
    GPtrArray *layers = g_ptr_array_new_with_free_func((GDestroyNotify)g_key_file_free);

    for (guint i = 0; i < paths->len; i++) {
        const char *path = g_ptr_array_index(paths, i);
        GKeyFile *kf = g_key_file_new();
        g_autoptr(GError) error = NULL;

        if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &error)) {
            g_ptr_array_add(layers, kf);
        } else {
            g_warning("Skipping %s: %s", path, error->message);
            g_key_file_free(kf);
        }
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

static gboolean lxqt_capture(LxpTheme *theme, GError **error)
{
    g_autoptr(GPtrArray) layers = load_layers();

    lxp_theme_remove_group(theme, GROUP);
    lxp_theme_remove_group(theme, GROUP_PALETTE);

    for (gsize k = 0; k < G_N_ELEMENTS(keys); k++) {
        for (guint i = layers->len; i > 0; i--) {
            GKeyFile *kf = g_ptr_array_index(layers, i - 1);
            g_autofree char *raw = g_key_file_get_value(kf, keys[k].group,
                                                        keys[k].key, NULL);
            if (raw) {
                g_autofree char *value = qt_unquote(raw);
                lxp_theme_set_string(theme, GROUP, keys[k].theme_key, value);
                break;
            }
        }
    }

    /* A custom palette is all-or-nothing: take the highest layer that has one. */
    for (guint i = layers->len; i > 0; i--) {
        GKeyFile *kf = g_ptr_array_index(layers, i - 1);
        g_auto(GStrv) names = g_key_file_get_keys(kf, "Palette", NULL, NULL);

        if (!names)
            continue;
        for (char **n = names; *n; n++) {
            g_autofree char *value = g_key_file_get_value(kf, "Palette", *n, NULL);
            lxp_theme_set_string(theme, GROUP_PALETTE, *n, value);
        }
        break;
    }
    return TRUE;
}

const LxpBackend lxp_backend_lxqt = {
    .id = "lxqt",
    .description = "LXQt theme, icons, Qt style, palette and font",
    .available = lxqt_available,
    .managed_files = lxqt_managed_files,
    .capture = lxqt_capture,
};
