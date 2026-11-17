/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LXQt panel appearance: ~/.config/lxqt/panel.conf. Only look-related
 * keys are themed; plugin layout is left alone. The same values are
 * applied to every panel listed in [General] panels.
 *
 * "menu_icon" is the icon of the menu button (the mainmenu or fancymenu
 * plugin): "theme" for the LXQt theme's own, or an image file, relative to
 * the theme folder or absolute. The panel is given a copy kept in
 * ~/.local/share/lxqt-personalizer/menu-icons, named after its contents,
 * so the icon outlives the theme and a rollback finds the previous one.
 *
 * lxqt-panel reads its settings at startup only, so reload() restarts it
 * through lxqt-session.
 */
#include "backends.h"

#include "config.h"
#include "lxp-error.h"
#include "lxp-fileutil.h"
#include "lxp-ini.h"
#include "lxp-paths.h"
#include "lxp-system.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>

#define GROUP         "Panel"
#define PANEL_CONF    "lxqt/panel.conf"
#define PANEL_MODULE  "lxqt-panel.desktop"
#define STOP_TIMEOUT  (5 * G_USEC_PER_SEC)
#define SETTLE_TIMEOUT (2 * G_USEC_PER_SEC)   /* for the session to see the panel quit */
#define START_TIMEOUT  (3 * G_USEC_PER_SEC)   /* for the panel to appear, per attempt */
#define START_ATTEMPTS 3
#define MENU_ICON_THEME "theme"
#define MENU_ICON_MAX   (2 * 1024 * 1024)

/* Values are copied raw (colors are Qt "@Variant(...)" blobs). */
static const char *const keys[] = {
    "position", "panelSize", "iconSize", "lineCount", "opacity",
    "background-color", "font-color", "background-image",
    "width", "width-percent", "alignment", "hidable", "reserve-space",
    NULL,
};

static const char *const positions[] = { "Top", "Bottom", "Left", "Right", NULL };

static GStrv panel_names(const LxpIni *ini)
{
    const char *raw = lxp_ini_get(ini, "General", "panels");
    g_autoptr(GStrvBuilder) builder = g_strv_builder_new();

    if (raw) {
        g_autofree char *list = lxp_qt_unquote(raw);
        g_auto(GStrv) names = g_strsplit(list, ",", -1);

        for (char **n = names; *n; n++) {
            g_strstrip(*n);
            if (**n)
                g_strv_builder_add(builder, *n);
        }
    } else if (lxp_ini_has_group(ini, "panel1")) {
        g_strv_builder_add(builder, "panel1");
    }
    return g_strv_builder_end(builder);
}

/* The user's panel.conf, or the system default LXQt would copy. */
static LxpIni *load_panel_conf(GError **error)
{
    g_autofree char *user = lxp_paths_user_config(PANEL_CONF);
    g_autofree char *source = lxp_paths_find_config(PANEL_CONF);

    return lxp_ini_load(source ? source : user, TRUE, error);
}

/*
 * Value of <group>/<key> as the panel sees it: the user's file, else the
 * system files it falls back to, key by key.
 */
static char *layered_get(const LxpIni *user, const char *group, const char *key)
{
    const char *value = lxp_ini_get(user, group, key);
    g_autoptr(GPtrArray) layers = NULL;

    if (value)
        return g_strdup(value);
    layers = lxp_paths_find_config_layers(PANEL_CONF);
    for (guint i = layers->len; i > 0; i--) {
        g_autoptr(LxpIni) ini = lxp_ini_load(g_ptr_array_index(layers, i - 1), TRUE, NULL);

        if (ini && (value = lxp_ini_get(ini, group, key)))
            return g_strdup(value);
    }
    return NULL;
}

/* Sections of the menu plugins (mainmenu, fancymenu) on <panels>. */
static GStrv menu_sections(const LxpIni *ini, char **panels)
{
    g_autoptr(GStrvBuilder) builder = g_strv_builder_new();

    for (char **p = panels; *p; p++) {
        g_autofree char *raw = layered_get(ini, *p, "plugins");
        g_autofree char *list = raw ? lxp_qt_unquote(raw) : NULL;
        g_auto(GStrv) plugins = list ? g_strsplit(list, ",", -1) : NULL;

        for (char **id = plugins; id && *id; id++) {
            g_autofree char *type = NULL;

            g_strstrip(*id);
            if (!**id || !(type = layered_get(ini, *id, "type")))
                continue;
            if (g_str_equal(type, "mainmenu") || g_str_equal(type, "fancymenu"))
                g_strv_builder_add(builder, *id);
        }
    }
    return g_strv_builder_end(builder);
}

static gboolean is_image_name(const char *path)
{
    static const char *const extensions[] = { ".svg", ".png", ".xpm", ".jpg", ".jpeg", NULL };
    g_autofree char *lower = g_ascii_strdown(path, -1);

    for (const char *const *e = extensions; *e; e++)
        if (g_str_has_suffix(lower, *e))
            return TRUE;
    return FALSE;
}

/*
 * The image file a theme's menu_icon names; NULL for none or "theme".
 * Fails for anything that is not a readable image of a sane size.
 */
static gboolean menu_icon_file(const LxpTheme *theme, char **file, GError **error)
{
    g_autofree char *value = lxp_theme_get_string(theme, GROUP, "menu_icon");
    g_autofree char *path = NULL;
    const char *dir = lxp_theme_get_dir(theme);
    GStatBuf st;

    *file = NULL;
    if (!value || !*value || g_str_equal(value, MENU_ICON_THEME))
        return TRUE;
    if (g_path_is_absolute(value)) {
        path = g_strdup(value);
    } else if (dir && !strstr(value, "..")) {
        path = g_build_filename(dir, value, NULL);
    }
    if (!path || !is_image_name(path) || g_stat(path, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size > MENU_ICON_MAX) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_NOT_FOUND,
                    "Menu button icon “%s” is not an image file that can be used", value);
        return FALSE;
    }
    *file = g_steal_pointer(&path);
    return TRUE;
}

/* Copy <file> to the menu icon store; returns the copy's path. */
static char *store_menu_icon(const char *file, GError **error)
{
    g_autofree char *data = NULL;
    g_autofree char *sum = NULL;
    g_autofree char *name = NULL;
    g_autofree char *lower = g_ascii_strdown(file, -1);
    const char *dot = strrchr(lower, '.');
    char *dest;
    gsize len;

    if (!g_file_get_contents(file, &data, &len, error))
        return NULL;
    sum = g_compute_checksum_for_data(G_CHECKSUM_SHA256, (const guchar *)data, len);
    name = g_strdup_printf("%.16s%s", sum, dot ? dot : "");
    dest = g_build_filename(g_get_user_data_dir(), LXP_APP_ID, "menu-icons", name, NULL);
    if (!g_file_test(dest, G_FILE_TEST_IS_REGULAR) &&
        !lxp_write_atomic(dest, data, len, 0644, error))
        g_clear_pointer(&dest, g_free);
    return dest;
}

static gboolean panel_available(void)
{
    g_autofree char *bin = g_find_program_in_path("lxqt-panel");
    return bin != NULL;
}

static GPtrArray *panel_managed_files(void)
{
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);

    g_ptr_array_add(files, lxp_paths_user_config(PANEL_CONF));
    return files;
}

static gboolean panel_check(const LxpTheme *theme, GError **error)
{
    g_autofree char *position = lxp_theme_get_string(theme, GROUP, "position");

    g_autofree char *icon = NULL;

    if (position && !g_strv_contains(positions, position)) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_INVALID,
                    "Invalid panel position “%s”", position);
        return FALSE;
    }
    return menu_icon_file(theme, &icon, error);
}

static gboolean panel_capture(LxpTheme *theme, GError **error)
{
    g_autoptr(LxpIni) ini = load_panel_conf(error);
    g_auto(GStrv) panels = NULL;

    if (!ini)
        return FALSE;

    lxp_theme_remove_group(theme, GROUP);
    panels = panel_names(ini);
    if (!panels[0])
        return TRUE;

    for (const char *const *k = keys; *k; k++) {
        const char *value = lxp_ini_get(ini, panels[0], *k);
        if (value)
            lxp_theme_set_string(theme, GROUP, *k, value);
    }

    /* the menu button: its own icon, or the LXQt theme's */
    {
        g_auto(GStrv) menus = menu_sections(ini, panels);

        if (menus[0]) {
            g_autofree char *own = layered_get(ini, menus[0], "ownIcon");
            g_autofree char *raw = layered_get(ini, menus[0], "icon");
            g_autofree char *icon = raw ? lxp_qt_unquote(raw) : NULL;
            gboolean custom = own && g_ascii_strcasecmp(own, "true") == 0 && icon &&
                              g_path_is_absolute(icon) &&
                              g_file_test(icon, G_FILE_TEST_IS_REGULAR);

            lxp_theme_set_string(theme, GROUP, "menu_icon", custom ? icon : MENU_ICON_THEME);
        }
    }
    return TRUE;
}

static gboolean panel_apply(const LxpTheme *theme, GError **error)
{
    GKeyFile *kf = lxp_theme_get_keyfile(theme);
    g_autofree char *path = lxp_paths_user_config(PANEL_CONF);
    g_autoptr(LxpIni) ini = NULL;
    g_auto(GStrv) panels = NULL;

    if (!g_key_file_has_group(kf, GROUP))
        return TRUE;
    if (!(ini = load_panel_conf(error)))
        return FALSE;

    panels = panel_names(ini);
    if (!panels[0]) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_NOT_FOUND,
                    "No panel defined in %s", path);
        return FALSE;
    }

    for (char **p = panels; *p; p++) {
        for (const char *const *k = keys; *k; k++) {
            g_autofree char *value = lxp_theme_get_string(theme, GROUP, *k);
            if (value)
                lxp_ini_set(ini, *p, *k, value);
        }
    }

    {
        g_autofree char *wanted = lxp_theme_get_string(theme, GROUP, "menu_icon");
        g_autofree char *file = NULL;
        g_autofree char *stored = NULL;
        g_auto(GStrv) menus = menu_sections(ini, panels);

        if (!menu_icon_file(theme, &file, error) ||
            (file && !(stored = store_menu_icon(file, error))))
            return FALSE;
        for (char **m = menus; wanted && *wanted && *m; m++) {
            if (stored) {
                g_autofree char *quoted = lxp_qt_quote(stored);

                lxp_ini_set(ini, *m, "ownIcon", "true");
                lxp_ini_set(ini, *m, "icon", quoted);
            } else {
                lxp_ini_set(ini, *m, "ownIcon", "false");
            }
        }
    }
    return lxp_ini_save(ini, path, error);
}

static void session_call(GDBusConnection *bus, const char *method)
{
    /* Both methods are NoReply: send and do not wait for an answer. */
    g_dbus_connection_call(bus, "org.lxqt.session", "/LXQtSession",
                           "org.lxqt.session", method,
                           g_variant_new("(s)", PANEL_MODULE), NULL,
                           G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    g_dbus_connection_flush_sync(bus, NULL, NULL);
}

/* Does lxqt-session still count the panel among its running modules? */
static gboolean session_runs_panel(GDBusConnection *bus)
{
    g_autoptr(GVariant) reply = g_dbus_connection_call_sync(
        bus, "org.lxqt.session", "/LXQtSession", "org.lxqt.session", "listModules",
        NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GVariant) list = NULL;
    g_autofree const char **names = NULL;

    if (!reply || g_variant_n_children(reply) != 1)
        return FALSE;  /* cannot tell: do not wait for it */
    value = g_variant_get_child_value(reply, 0);
    list = g_variant_is_of_type(value, G_VARIANT_TYPE_VARIANT) ? g_variant_get_variant(value)
                                                               : g_variant_ref(value);
    if (!g_variant_is_of_type(list, G_VARIANT_TYPE_STRING_ARRAY))
        return FALSE;
    names = g_variant_get_strv(list, NULL);
    return g_strv_contains(names, PANEL_MODULE);
}

static gboolean panel_reload(GError **error)
{
    g_autofree char *path = lxp_paths_user_config(PANEL_CONF);
    g_autofree char *wanted = NULL;
    g_autofree char *after = NULL;
    g_auto(GStrv) running = lxp_process_find("lxqt-panel");
    g_autoptr(GDBusConnection) bus = NULL;
    gint64 deadline;

    if (!running)
        return TRUE;
    if (!g_file_get_contents(path, &wanted, NULL, error))
        return FALSE;
    if (!(bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, error)))
        return FALSE;

    session_call(bus, "stopModule");
    deadline = g_get_monotonic_time() + STOP_TIMEOUT;
    for (;;) {
        g_auto(GStrv) still = lxp_process_find("lxqt-panel");
        if (!still)
            break;
        if (g_get_monotonic_time() > deadline) {
            g_set_error(error, LXP_ERROR, LXP_ERROR_FAILED,
                        "lxqt-panel did not stop; restart it to see the changes");
            return FALSE;
        }
        g_usleep(100 * 1000);
    }

    /* The panel may save its old in-memory settings when quitting. */
    if (!g_file_get_contents(path, &after, NULL, NULL) || !g_str_equal(after, wanted)) {
        if (!lxp_write_atomic(path, wanted, -1, 0644, error))
            return FALSE;
    }

    /*
     * The session only starts a module it knows to be stopped, and it may
     * learn that the panel quit a moment after the process is gone: a start
     * asked too early is ignored, leaving no panel at all. So wait for the
     * session, then make sure the panel really comes back.
     */
    deadline = g_get_monotonic_time() + SETTLE_TIMEOUT;
    while (session_runs_panel(bus) && g_get_monotonic_time() < deadline)
        g_usleep(50 * 1000);

    for (int attempt = 0; attempt < START_ATTEMPTS; attempt++) {
        session_call(bus, "startModule");
        deadline = g_get_monotonic_time() + START_TIMEOUT;
        while (g_get_monotonic_time() < deadline) {
            g_auto(GStrv) started = lxp_process_find("lxqt-panel");
            if (started)
                return TRUE;
            g_usleep(100 * 1000);
        }
    }
    g_set_error_literal(error, LXP_ERROR, LXP_ERROR_FAILED,
                        "lxqt-panel did not start again; start it with “lxqt-panel”");
    return FALSE;
}

const LxpBackend lxp_backend_panel = {
    .id = "panel",
    .description = "LXQt panel position, size, colors and opacity",
    .available = panel_available,
    .managed_files = panel_managed_files,
    .check = panel_check,
    .capture = panel_capture,
    .apply = panel_apply,
    .reload = panel_reload,
};
