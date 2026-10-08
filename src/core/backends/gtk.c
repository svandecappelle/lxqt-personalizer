/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * GTK 2/3/4 appearance: ~/.config/gtk-3.0/settings.ini,
 * ~/.config/gtk-4.0/settings.ini and ~/.gtkrc-2.0 (the files
 * lxqt-config-appearance writes). Running GTK apps only follow
 * org.gnome.desktop.interface (GSettings), which reload() updates.
 */
#include "backends.h"

#include "lxp-ini.h"
#include "lxp-paths.h"
#include "lxp-system.h"

#include <gio/gio.h>
#include <string.h>

#define GROUP        "GTK"
#define GTK3_INI     "gtk-3.0/settings.ini"
#define GTK4_INI     "gtk-4.0/settings.ini"
#define INI_GROUP    "Settings"
#define IFACE_SCHEMA "org.gnome.desktop.interface"

static const char *const builtin_themes[] = {
    "Adwaita", "HighContrast", "HighContrastInverse", "Default", NULL,
};

static char *gtk2_rc_path(void)
{
    return g_build_filename(g_get_home_dir(), ".gtkrc-2.0", NULL);
}

GPtrArray *lxp_gtk_settings_files(void)
{
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);

    g_ptr_array_add(files, lxp_paths_user_config(GTK3_INI));
    g_ptr_array_add(files, lxp_paths_user_config(GTK4_INI));
    g_ptr_array_add(files, gtk2_rc_path());
    return files;
}

static char *gtk2_unquote(const char *raw)
{
    size_t len = raw ? strlen(raw) : 0;

    if (len >= 2 && raw[0] == '"' && raw[len - 1] == '"')
        return g_strndup(raw + 1, len - 2);
    return g_strdup(raw);
}

gboolean lxp_gtk_write_settings(const LxpGtkSetting *settings, gsize n,
                                GError **error)
{
    g_autofree char *gtk3_path = lxp_paths_user_config(GTK3_INI);
    g_autofree char *gtk4_path = lxp_paths_user_config(GTK4_INI);
    g_autofree char *gtk2_path = gtk2_rc_path();
    g_autoptr(LxpIni) gtk3 = lxp_ini_load(gtk3_path, TRUE, error);
    g_autoptr(LxpIni) gtk4 = gtk3 ? lxp_ini_load(gtk4_path, TRUE, error) : NULL;
    g_autoptr(LxpIni) gtk2 = gtk4 ? lxp_ini_load(gtk2_path, TRUE, error) : NULL;
    gboolean any = FALSE, any4 = FALSE;

    if (!gtk2)
        return FALSE;

    for (gsize i = 0; i < n; i++) {
        const LxpGtkSetting *s = &settings[i];

        if (!s->value)
            continue;
        any = TRUE;
        lxp_ini_set(gtk3, INI_GROUP, s->key, s->value);
        if (s->gtk4) {
            lxp_ini_set(gtk4, INI_GROUP, s->key, s->value);
            any4 = TRUE;
        }
        if (s->is_string) {
            g_autofree char *quoted = g_strdup_printf("\"%s\"", s->value);
            lxp_ini_set(gtk2, NULL, s->key, quoted);
        } else {
            lxp_ini_set(gtk2, NULL, s->key, s->value);
        }
    }

    if (!any)
        return TRUE;
    return lxp_ini_save(gtk3, gtk3_path, error) &&
           (!any4 || lxp_ini_save(gtk4, gtk4_path, error)) &&
           lxp_ini_save(gtk2, gtk2_path, error);
}

/* --- GSettings ---------------------------------------------------------- */

static GSettings *interface_settings(void)
{
    static GSettings *settings;
    static gboolean looked_up;

    if (!looked_up) {
        GSettingsSchemaSource *source = g_settings_schema_source_get_default();
        g_autoptr(GSettingsSchema) schema = source
            ? g_settings_schema_source_lookup(source, IFACE_SCHEMA, TRUE)
            : NULL;

        looked_up = TRUE;
        if (schema)
            settings = g_settings_new_full(schema, NULL, NULL);
    }
    return settings;
}

static gboolean has_key(GSettings *settings, const char *key)
{
    g_autoptr(GSettingsSchema) schema = NULL;

    if (!settings)
        return FALSE;
    g_object_get(settings, "settings-schema", &schema, NULL);
    return g_settings_schema_has_key(schema, key);
}

void lxp_gsettings_set(const char *key, const char *value)
{
    GSettings *settings = interface_settings();

    if (value && has_key(settings, key))
        g_settings_set_string(settings, key, value);
}

void lxp_gsettings_set_int(const char *key, int value)
{
    GSettings *settings = interface_settings();

    if (has_key(settings, key))
        g_settings_set_int(settings, key, value);
}

void lxp_gsettings_sync(void)
{
    if (interface_settings())
        g_settings_sync();
}

/* --- Backend ------------------------------------------------------------ */

static gboolean is_true(const char *value)
{
    return value && (g_str_equal(value, "1") || g_ascii_strcasecmp(value, "true") == 0);
}

static gboolean gtk_available(void)
{
    return TRUE;
}

static GPtrArray *gtk_managed_files(void)
{
    return lxp_gtk_settings_files();
}

static gboolean gtk_check(const LxpTheme *theme, GError **error)
{
    g_autofree char *name = lxp_theme_get_string(theme, GROUP, "theme");
    g_autofree char *icons = lxp_theme_get_string(theme, GROUP, "icon_theme");
    g_autofree char *found = NULL;

    if (name && !g_strv_contains(builtin_themes, name)) {
        found = lxp_backend_locate(theme, "themes", name, "gtk-3.0");
        if (!found)
            found = lxp_backend_locate(theme, "themes", name, "gtk-2.0");
        if (!lxp_backend_require(g_steal_pointer(&found), "GTK theme", name, error))
            return FALSE;
    }
    if (icons && !lxp_backend_require(lxp_backend_locate(theme, "icons", icons, "index.theme"),
                                      "Icon theme", icons, error))
        return FALSE;
    return TRUE;
}

static gboolean gtk_capture(LxpTheme *theme, GError **error)
{
    g_autofree char *gtk3_path = lxp_paths_user_config(GTK3_INI);
    g_autofree char *gtk2_path = gtk2_rc_path();
    g_autoptr(LxpIni) gtk3 = lxp_ini_load(gtk3_path, TRUE, error);
    g_autoptr(LxpIni) gtk2 = gtk3 ? lxp_ini_load(gtk2_path, TRUE, error) : NULL;
    static const struct { const char *key, *theme_key; } keys[] = {
        { "gtk-theme-name",                    "theme" },
        { "gtk-icon-theme-name",               "icon_theme" },
        { "gtk-font-name",                     "font" },
        { "gtk-application-prefer-dark-theme", "prefer_dark" },
    };

    if (!gtk2)
        return FALSE;

    lxp_theme_remove_group(theme, GROUP);
    for (gsize i = 0; i < G_N_ELEMENTS(keys); i++) {
        const char *value = lxp_ini_get(gtk3, INI_GROUP, keys[i].key);
        g_autofree char *fallback = NULL;

        if (!value)
            value = fallback = gtk2_unquote(lxp_ini_get(gtk2, NULL, keys[i].key));
        if (!value)
            continue;
        if (g_str_equal(keys[i].theme_key, "prefer_dark"))
            value = is_true(value) ? "true" : "false";
        lxp_theme_set_string(theme, GROUP, keys[i].theme_key, value);
    }
    return TRUE;
}

static gboolean gtk_apply(const LxpTheme *theme, GError **error)
{
    g_autofree char *name = lxp_theme_get_string(theme, GROUP, "theme");
    g_autofree char *icons = lxp_theme_get_string(theme, GROUP, "icon_theme");
    g_autofree char *font = lxp_theme_get_string(theme, GROUP, "font");
    g_autofree char *dark = lxp_theme_get_string(theme, GROUP, "prefer_dark");
    g_autofree char *gtk4_dir = NULL;
    gboolean gtk4_theme;

    /* Only point GTK 4 at themes that ship a gtk-4.0 variant. */
    gtk4_theme = name && (g_strv_contains(builtin_themes, name) ||
                          (gtk4_dir = lxp_backend_locate(theme, "themes", name, "gtk-4.0")));
    {
        const LxpGtkSetting settings[] = {
            { "gtk-theme-name", name, TRUE, gtk4_theme },
            { "gtk-icon-theme-name", icons, TRUE, TRUE },
            { "gtk-font-name", font, TRUE, TRUE },
            { "gtk-application-prefer-dark-theme",
              dark ? (is_true(dark) ? "1" : "0") : NULL, FALSE, TRUE },
        };
        return lxp_gtk_write_settings(settings, G_N_ELEMENTS(settings), error);
    }
}

static gboolean gtk_reload(GError **error)
{
    g_autofree char *path = lxp_paths_user_config(GTK3_INI);
    g_autoptr(LxpIni) ini = lxp_ini_load(path, TRUE, error);
    const char *dark;

    if (!ini)
        return FALSE;

    lxp_gsettings_set("gtk-theme", lxp_ini_get(ini, INI_GROUP, "gtk-theme-name"));
    lxp_gsettings_set("icon-theme", lxp_ini_get(ini, INI_GROUP, "gtk-icon-theme-name"));
    lxp_gsettings_set("font-name", lxp_ini_get(ini, INI_GROUP, "gtk-font-name"));
    dark = lxp_ini_get(ini, INI_GROUP, "gtk-application-prefer-dark-theme");
    if (dark)
        lxp_gsettings_set("color-scheme", is_true(dark) ? "prefer-dark" : "default");
    lxp_gsettings_sync();
    return TRUE;
}

const LxpBackend lxp_backend_gtk = {
    .id = "gtk",
    .description = "GTK 2/3/4 theme, icons, font and dark preference",
    .available = gtk_available,
    .managed_files = gtk_managed_files,
    .check = gtk_check,
    .capture = gtk_capture,
    .apply = gtk_apply,
    .reload = gtk_reload,
};
