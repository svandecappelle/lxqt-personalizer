/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LXQt panel appearance: ~/.config/lxqt/panel.conf. Only look-related
 * keys are themed; plugin layout is left alone. The same values are
 * applied to every panel listed in [General] panels.
 *
 * lxqt-panel reads its settings at startup only, so reload() restarts it
 * through lxqt-session.
 */
#include "backends.h"

#include "lxp-error.h"
#include "lxp-fileutil.h"
#include "lxp-ini.h"
#include "lxp-paths.h"
#include "lxp-system.h"

#include <gio/gio.h>

#define GROUP         "Panel"
#define PANEL_CONF    "lxqt/panel.conf"
#define PANEL_MODULE  "lxqt-panel.desktop"
#define STOP_TIMEOUT  (5 * G_USEC_PER_SEC)

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

    if (position && !g_strv_contains(positions, position)) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_INVALID,
                    "Invalid panel position “%s”", position);
        return FALSE;
    }
    return TRUE;
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

    session_call(bus, "startModule");
    return TRUE;
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
