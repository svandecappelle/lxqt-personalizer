/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Mouse cursor theme. Written everywhere it is read from:
 *   ~/.config/lxqt/session.conf [Mouse]  (LXQt sets XCURSOR_* at login)
 *   ~/.icons/default/index.theme         (X fallback cursor theme)
 *   ~/.Xresources, ~/.Xdefaults          (Xcursor.theme / Xcursor.size)
 *   GTK settings files                   (gtk-cursor-theme-name/size)
 * Running apps keep their cursor until restarted; a full switch needs a
 * new session.
 */
#include "backends.h"

#include "lxp-error.h"
#include "lxp-fileutil.h"
#include "lxp-ini.h"
#include "lxp-paths.h"
#include "lxp-system.h"

#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

#define GROUP        "Cursor"
#define SESSION_CONF "lxqt/session.conf"

static char *home_file(const char *name)
{
    return g_build_filename(g_get_home_dir(), name, NULL);
}

static char *index_theme_path(void)
{
    return g_build_filename(g_get_home_dir(), ".icons", "default", "index.theme", NULL);
}

/* Replace or append "<resource>: <value>" in an X resources file. */
static gboolean set_xresource(const char *path, const char *resource,
                              const char *value, GError **error)
{
    g_autofree char *data = NULL;
    g_auto(GStrv) lines = NULL;
    GString *out = g_string_new(NULL);
    gboolean found = FALSE;
    GStatBuf st;
    int mode = 0644;

    if (!g_file_get_contents(path, &data, NULL, NULL))
        data = g_strdup("");
    if (g_stat(path, &st) == 0)
        mode = st.st_mode & 07777;

    lines = g_strsplit(data, "\n", -1);
    for (char **l = lines; *l; l++) {
        const char *p = *l;
        size_t len = strlen(resource);

        if (!l[1] && !**l)  /* trailing empty piece after the last \n */
            break;
        while (g_ascii_isspace(*p))
            p++;
        if (!found && strncmp(p, resource, len) == 0) {
            const char *q = p + len;
            while (*q == ' ' || *q == '\t')
                q++;
            if (*q == ':') {
                g_string_append_printf(out, "%s: %s\n", resource, value);
                found = TRUE;
                continue;
            }
        }
        g_string_append_printf(out, "%s\n", *l);
    }
    if (!found)
        g_string_append_printf(out, "%s: %s\n", resource, value);

    {
        g_autofree char *text = g_string_free(out, FALSE);
        return lxp_write_atomic(path, text, -1, mode, error);
    }
}

static gboolean cursor_available(void)
{
    return TRUE;
}

static GPtrArray *cursor_managed_files(void)
{
    GPtrArray *files = lxp_gtk_settings_files();

    g_ptr_array_add(files, lxp_paths_user_config(SESSION_CONF));
    g_ptr_array_add(files, index_theme_path());
    g_ptr_array_add(files, home_file(".Xresources"));
    g_ptr_array_add(files, home_file(".Xdefaults"));
    return files;
}

static gboolean cursor_check(const LxpTheme *theme, GError **error)
{
    g_autofree char *name = lxp_theme_get_string(theme, GROUP, "theme");
    g_autofree char *size = lxp_theme_get_string(theme, GROUP, "size");
    guint64 n;

    if (size && (!g_ascii_string_to_unsigned(size, 10, 8, 256, &n, NULL))) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_INVALID,
                    "Invalid cursor size “%s” (expected 8-256)", size);
        return FALSE;
    }
    if (!name)
        return TRUE;
    return lxp_backend_require(lxp_find_installed("icons", name, "cursors"),
                               "Cursor theme", name, error);
}

static gboolean cursor_capture(LxpTheme *theme, GError **error)
{
    g_autofree char *session_path = lxp_paths_user_config(SESSION_CONF);
    g_autofree char *index_path = index_theme_path();
    g_autoptr(LxpIni) session = lxp_ini_load(session_path, TRUE, error);
    g_autoptr(LxpIni) index = session ? lxp_ini_load(index_path, TRUE, error) : NULL;
    const char *name, *size;

    if (!index)
        return FALSE;

    lxp_theme_remove_group(theme, GROUP);
    name = lxp_ini_get(session, "Mouse", "cursor_theme");
    if (!name)
        name = lxp_ini_get(index, "Icon Theme", "Inherits");
    size = lxp_ini_get(session, "Mouse", "cursor_size");
    if (!size)
        size = lxp_ini_get(index, "Icon Theme", "Size");

    if (name)
        lxp_theme_set_string(theme, GROUP, "theme", name);
    if (size)
        lxp_theme_set_string(theme, GROUP, "size", size);
    return TRUE;
}

static gboolean cursor_apply(const LxpTheme *theme, GError **error)
{
    g_autofree char *name = lxp_theme_get_string(theme, GROUP, "theme");
    g_autofree char *size = lxp_theme_get_string(theme, GROUP, "size");
    g_autofree char *session_path = lxp_paths_user_config(SESSION_CONF);
    g_autofree char *index_path = index_theme_path();
    g_autofree char *xresources = home_file(".Xresources");
    g_autofree char *xdefaults = home_file(".Xdefaults");
    g_autoptr(LxpIni) session = NULL;
    g_autoptr(LxpIni) index = NULL;

    if (!name && !size)
        return TRUE;

    if (!(session = lxp_ini_load(session_path, TRUE, error)) ||
        !(index = lxp_ini_load(index_path, TRUE, error)))
        return FALSE;

    if (!lxp_ini_has_group(index, "Icon Theme")) {
        lxp_ini_set(index, "Icon Theme", "Name", "Default");
        lxp_ini_set(index, "Icon Theme", "Comment", "Default cursor theme");
    }
    if (name) {
        lxp_ini_set(session, "Mouse", "cursor_theme", name);
        lxp_ini_set(index, "Icon Theme", "Inherits", name);
    }
    if (size) {
        lxp_ini_set(session, "Mouse", "cursor_size", size);
        lxp_ini_set(index, "Icon Theme", "Size", size);
    }
    if (!lxp_ini_save(session, session_path, error) ||
        !lxp_ini_save(index, index_path, error))
        return FALSE;

    /* .Xresources is always kept in sync; .Xdefaults only if it exists. */
    for (int i = 0; i < 2; i++) {
        const char *path = i == 0 ? xresources : xdefaults;

        if (i == 1 && !g_file_test(path, G_FILE_TEST_EXISTS))
            continue;
        if ((name && !set_xresource(path, "Xcursor.theme", name, error)) ||
            (size && !set_xresource(path, "Xcursor.size", size, error)))
            return FALSE;
    }

    {
        const LxpGtkSetting settings[] = {
            { "gtk-cursor-theme-name", name, TRUE, TRUE },
            { "gtk-cursor-theme-size", size, FALSE, TRUE },
        };
        return lxp_gtk_write_settings(settings, G_N_ELEMENTS(settings), error);
    }
}

static gboolean cursor_reload(GError **error)
{
    g_autofree char *session_path = lxp_paths_user_config(SESSION_CONF);
    g_autofree char *xresources = home_file(".Xresources");
    g_autoptr(LxpIni) session = lxp_ini_load(session_path, TRUE, error);
    g_autofree char *xrdb = g_find_program_in_path("xrdb");
    const char *size;

    if (!session)
        return FALSE;

    if (xrdb && g_getenv("DISPLAY") && g_file_test(xresources, G_FILE_TEST_EXISTS)) {
        const char *argv[] = { "xrdb", "-merge", xresources, NULL };
        if (!lxp_run(argv, error))
            return FALSE;
    }

    lxp_gsettings_set("cursor-theme", lxp_ini_get(session, "Mouse", "cursor_theme"));
    size = lxp_ini_get(session, "Mouse", "cursor_size");
    if (size)
        lxp_gsettings_set_int("cursor-size", atoi(size));
    lxp_gsettings_sync();
    return TRUE;
}

const LxpBackend lxp_backend_cursor = {
    .id = "cursor",
    .description = "Mouse cursor theme and size",
    .available = cursor_available,
    .managed_files = cursor_managed_files,
    .check = cursor_check,
    .capture = cursor_capture,
    .apply = cursor_apply,
    .reload = cursor_reload,
};
