/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-auto.h"

#include "config.h"
#include "lxp-error.h"
#include "lxp-fileutil.h"
#include "lxp-paths.h"

#include <glib/gstdio.h>
#include <errno.h>
#include <string.h>

#define GROUP "Auto"
#define DAY   (24 * 60)

gboolean lxp_auto_parse_time(const char *text, int *minutes)
{
    const char *colon = text ? strchr(text, ':') : NULL;
    gsize hours_len, minutes_len;
    int h, m;

    if (!colon)
        return FALSE;
    hours_len = colon - text;
    minutes_len = strlen(colon + 1);
    if (hours_len < 1 || hours_len > 2 || minutes_len != 2)
        return FALSE;
    for (const char *p = text; *p; p++)
        if (p != colon && !g_ascii_isdigit(*p))
            return FALSE;

    h = (int)g_ascii_strtoll(text, NULL, 10);
    m = (int)g_ascii_strtoll(colon + 1, NULL, 10);
    if (h > 23 || m > 59)
        return FALSE;
    *minutes = h * 60 + m;
    return TRUE;
}

char *lxp_auto_format_time(int minutes)
{
    return g_strdup_printf("%02d:%02d", minutes / 60, minutes % 60);
}

static char *config_path(void)
{
    return lxp_paths_user_config(LXP_APP_ID "/auto.conf");
}

static char *state_path(void)
{
    return g_build_filename(g_get_user_state_dir(), LXP_APP_ID, "auto-state", NULL);
}

void lxp_auto_config_free(LxpAutoConfig *config)
{
    if (!config)
        return;
    g_free(config->theme);
    g_free(config);
}

static gboolean valid(const LxpAutoConfig *config, GError **error)
{
    if (!config->theme || !*config->theme || strchr(config->theme, '\n')) {
        g_set_error_literal(error, LXP_ERROR, LXP_ERROR_INVALID,
                            "Automatic switching needs a theme");
        return FALSE;
    }
    if (config->light < 0 || config->light >= DAY || config->dark < 0 ||
        config->dark >= DAY || config->light == config->dark) {
        g_set_error_literal(error, LXP_ERROR, LXP_ERROR_INVALID,
                            "The light and dark times must be two different times of day");
        return FALSE;
    }
    return TRUE;
}

LxpAutoConfig *lxp_auto_load(GError **error)
{
    g_autofree char *path = config_path();
    g_autoptr(GKeyFile) kf = g_key_file_new();
    g_autoptr(LxpAutoConfig) config = g_new0(LxpAutoConfig, 1);
    g_autofree char *light = NULL;
    g_autofree char *dark = NULL;

    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        g_set_error_literal(error, LXP_ERROR, LXP_ERROR_NOT_FOUND,
                            "Automatic light/dark switching is not set up "
                            "(see “lxp auto on --help”)");
        return NULL;
    }
    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, error))
        return NULL;

    config->theme = g_key_file_get_string(kf, GROUP, "theme", NULL);
    light = g_key_file_get_string(kf, GROUP, "light", NULL);
    dark = g_key_file_get_string(kf, GROUP, "dark", NULL);
    config->light = LXP_AUTO_DEFAULT_LIGHT;
    config->dark = LXP_AUTO_DEFAULT_DARK;
    if ((light && !lxp_auto_parse_time(light, &config->light)) ||
        (dark && !lxp_auto_parse_time(dark, &config->dark))) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_INVALID,
                    "%s: times must be written as HH:MM", path);
        return NULL;
    }
    if (!valid(config, error)) {
        g_prefix_error(error, "%s: ", path);
        return NULL;
    }
    return g_steal_pointer(&config);
}

gboolean lxp_auto_save(const LxpAutoConfig *config, GError **error)
{
    g_autofree char *path = config_path();
    g_autoptr(GKeyFile) kf = g_key_file_new();
    g_autofree char *light = NULL;
    g_autofree char *dark = NULL;
    g_autofree char *data = NULL;

    if (!valid(config, error))
        return FALSE;
    light = lxp_auto_format_time(config->light);
    dark = lxp_auto_format_time(config->dark);
    g_key_file_set_string(kf, GROUP, "theme", config->theme);
    g_key_file_set_string(kf, GROUP, "light", light);
    g_key_file_set_string(kf, GROUP, "dark", dark);
    data = g_key_file_to_data(kf, NULL, NULL);
    return lxp_write_atomic(path, data, -1, 0644, error);
}

gboolean lxp_auto_wants_dark(const LxpAutoConfig *config, int minute)
{
    /* light from <light> to <dark>, which may be on either side of midnight */
    if (config->light < config->dark)
        return minute < config->light || minute >= config->dark;
    return minute >= config->dark && minute < config->light;
}

char *lxp_auto_units_dir(void)
{
    return lxp_paths_user_config("systemd/user");
}

/* Quote <path> for an Exec line of a unit file. */
static char *unit_quote(const char *path)
{
    GString *out = g_string_new("\"");

    for (const char *p = path; *p; p++) {
        if (*p == '"' || *p == '\\')
            g_string_append_c(out, '\\');
        if (*p == '%' || *p == '$')
            g_string_append_c(out, *p);  /* %% and $$ are the literal characters */
        g_string_append_c(out, *p);
    }
    g_string_append_c(out, '"');
    return g_string_free(out, FALSE);
}

gboolean lxp_auto_write_units(const LxpAutoConfig *config, const char *lxp, GError **error)
{
    g_autofree char *dir = lxp_auto_units_dir();
    g_autofree char *service_path = g_build_filename(dir, LXP_AUTO_SERVICE, NULL);
    g_autofree char *timer_path = g_build_filename(dir, LXP_AUTO_TIMER, NULL);
    g_autofree char *exec = NULL;
    g_autofree char *light = NULL;
    g_autofree char *dark = NULL;
    g_autofree char *service = NULL;
    g_autofree char *timer = NULL;

    if (!valid(config, error))
        return FALSE;
    if (!g_path_is_absolute(lxp) || strchr(lxp, '\n')) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_INVALID,
                    "“%s” is not an absolute program path", lxp);
        return FALSE;
    }
    exec = unit_quote(lxp);
    light = lxp_auto_format_time(config->light);
    dark = lxp_auto_format_time(config->dark);

    service = g_strdup_printf(
        "# Written by lxp auto on; removed by lxp auto off.\n"
        "[Unit]\n"
        "Description=Switch the desktop theme between light and dark\n"
        "\n"
        "[Service]\n"
        "Type=oneshot\n"
        "ExecStart=%s auto run\n", exec);
    timer = g_strdup_printf(
        "# Written by lxp auto on; removed by lxp auto off.\n"
        "[Unit]\n"
        "Description=Light and dark theme schedule\n"
        "\n"
        "[Timer]\n"
        "OnCalendar=*-*-* %s:00\n"
        "OnCalendar=*-*-* %s:00\n"
        "Persistent=true\n"
        "AccuracySec=1min\n"
        "\n"
        "[Install]\n"
        "WantedBy=timers.target\n", light, dark);

    return lxp_write_atomic(service_path, service, -1, 0644, error) &&
           lxp_write_atomic(timer_path, timer, -1, 0644, error);
}

static char *state_line(const char *theme, gboolean dark)
{
    return g_strdup_printf("%s %s\n", theme, dark ? "dark" : "light");
}

char *lxp_auto_get_state(void)
{
    g_autofree char *path = state_path();
    char *state = NULL;

    if (!g_file_get_contents(path, &state, NULL, NULL))
        return NULL;
    return g_strchomp(state);
}

gboolean lxp_auto_set_state(const char *theme, gboolean dark, GError **error)
{
    g_autofree char *path = state_path();
    g_autofree char *line = state_line(theme, dark);

    return lxp_write_atomic(path, line, -1, 0644, error);
}

gboolean lxp_auto_state_matches(const char *theme, gboolean dark)
{
    g_autofree char *state = lxp_auto_get_state();
    g_autofree char *line = state_line(theme, dark);

    return state && g_str_equal(state, g_strchomp(line));
}

static gboolean remove_file(const char *path, GError **error)
{
    if (g_unlink(path) == 0 || errno == ENOENT)
        return TRUE;
    g_set_error(error, LXP_ERROR, LXP_ERROR_FAILED, "Cannot remove %s: %s", path,
                g_strerror(errno));
    return FALSE;
}

gboolean lxp_auto_remove(GError **error)
{
    g_autofree char *dir = lxp_auto_units_dir();
    g_autofree char *service = g_build_filename(dir, LXP_AUTO_SERVICE, NULL);
    g_autofree char *timer = g_build_filename(dir, LXP_AUTO_TIMER, NULL);
    g_autofree char *config = config_path();
    g_autofree char *state = state_path();

    return remove_file(timer, error) && remove_file(service, error) &&
           remove_file(config, error) && remove_file(state, error);
}
