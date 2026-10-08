/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Compositor effects: rounded corners, shadows, fading and transparency,
 * drawn by picom (Openbox cannot round corners itself). picom reads the
 * first of $XDG_CONFIG_HOME/picom.conf, $XDG_CONFIG_HOME/picom/picom.conf
 * and $XDG_CONFIG_DIRS/picom.conf, and rereads it on SIGUSR1.
 */
#include "backends.h"

#include "lxp-error.h"
#include "lxp-libconfig.h"
#include "lxp-paths.h"
#include "lxp-system.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>

#define GROUP "Compositor"

typedef enum { TYPE_INT, TYPE_BOOL, TYPE_FRACTION } SettingType;

static const struct {
    const char *theme_key;
    const char *picom_key;
    SettingType type;
    int min, max;
} settings[] = {
    { "corner_radius",    "corner-radius",    TYPE_INT,      0, 64 },
    { "shadow",           "shadow",           TYPE_BOOL,     0, 0 },
    { "shadow_radius",    "shadow-radius",    TYPE_INT,      0, 100 },
    { "shadow_opacity",   "shadow-opacity",   TYPE_FRACTION, 0, 0 },
    { "shadow_offset_x",  "shadow-offset-x",  TYPE_INT,      -100, 100 },
    { "shadow_offset_y",  "shadow-offset-y",  TYPE_INT,      -100, 100 },
    { "fading",           "fading",           TYPE_BOOL,     0, 0 },
    { "inactive_opacity", "inactive-opacity", TYPE_FRACTION, 0, 0 },
};

/* The user config picom uses: an existing one, else ~/.config/picom.conf. */
static char *user_config_path(void)
{
    g_autofree char *nested = lxp_paths_user_config("picom/picom.conf");
    char *flat = lxp_paths_user_config("picom.conf");

    if (!g_file_test(flat, G_FILE_TEST_EXISTS) && g_file_test(nested, G_FILE_TEST_EXISTS)) {
        g_free(flat);
        return g_steal_pointer(&nested);
    }
    return flat;
}

/* The config picom actually reads (user, else system), or NULL. */
static char *active_config_path(void)
{
    g_autofree char *user = user_config_path();
    g_autofree char *nested = lxp_paths_user_config("picom/picom.conf");

    if (g_file_test(user, G_FILE_TEST_IS_REGULAR))
        return g_steal_pointer(&user);
    if (g_file_test(nested, G_FILE_TEST_IS_REGULAR))
        return g_steal_pointer(&nested);
    for (const char *const *d = g_get_system_config_dirs(); *d; d++) {
        char *path = g_build_filename(*d, "picom.conf", NULL);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
            return path;
        g_free(path);
    }
    return NULL;
}

/* Normalize a value to picom syntax; NULL if invalid. */
static char *normalize(SettingType type, const char *value, int min, int max)
{
    char *end;

    if (!value)
        return NULL;
    switch (type) {
    case TYPE_BOOL:
        if (g_ascii_strcasecmp(value, "true") == 0 || g_str_equal(value, "1"))
            return g_strdup("true");
        if (g_ascii_strcasecmp(value, "false") == 0 || g_str_equal(value, "0"))
            return g_strdup("false");
        return NULL;
    case TYPE_INT: {
        long v = strtol(value, &end, 10);
        if (end == value || *end || v < min || v > max)
            return NULL;
        return g_strdup_printf("%ld", v);
    }
    case TYPE_FRACTION: {
        double v = g_ascii_strtod(value, &end);
        char buf[G_ASCII_DTOSTR_BUF_SIZE];
        if (end == value || *end || v < 0 || v > 1)
            return NULL;
        return g_strdup(g_ascii_formatd(buf, sizeof(buf), "%g", v));
    }
    }
    return NULL;
}

static gboolean compositor_available(void)
{
    g_autofree char *bin = g_find_program_in_path("picom");
    return bin != NULL;
}

static GPtrArray *compositor_managed_files(void)
{
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);

    g_ptr_array_add(files, user_config_path());
    return files;
}

static gboolean compositor_check(const LxpTheme *theme, GError **error)
{
    const char *argv[] = { "picom", "--version", NULL };

    if (!g_key_file_has_group(lxp_theme_get_keyfile(theme), GROUP))
        return TRUE;

    for (gsize i = 0; i < G_N_ELEMENTS(settings); i++) {
        g_autofree char *value = lxp_theme_get_string(theme, GROUP, settings[i].theme_key);
        g_autofree char *norm = NULL;

        if (value && !(norm = normalize(settings[i].type, value, settings[i].min,
                                        settings[i].max))) {
            g_set_error(error, LXP_ERROR, LXP_ERROR_INVALID,
                        "Invalid %s “%s”", settings[i].theme_key, value);
            return FALSE;
        }
    }

    /* A picom that cannot start (e.g. a missing library) shows nothing. */
    if (!lxp_run(argv, error)) {
        g_prefix_error(error, "picom is installed but does not start: ");
        return FALSE;
    }
    return TRUE;
}

static gboolean compositor_capture(LxpTheme *theme, GError **error)
{
    g_autofree char *path = active_config_path();
    g_autoptr(LxpLibconfig) conf = NULL;

    lxp_theme_remove_group(theme, GROUP);
    if (!path)
        return TRUE;  /* picom defaults: nothing to record */
    if (!(conf = lxp_libconfig_load(path, FALSE, error)))
        return FALSE;

    for (gsize i = 0; i < G_N_ELEMENTS(settings); i++) {
        g_autofree char *raw = lxp_libconfig_get(conf, settings[i].picom_key);
        g_autofree char *norm = normalize(settings[i].type, raw, settings[i].min,
                                          settings[i].max);
        if (norm)
            lxp_theme_set_string(theme, GROUP, settings[i].theme_key, norm);
    }
    return TRUE;
}

static gboolean compositor_apply(const LxpTheme *theme, GError **error)
{
    g_autofree char *target = user_config_path();
    g_autofree char *source = NULL;
    g_autoptr(LxpLibconfig) conf = NULL;

    if (!g_key_file_has_group(lxp_theme_get_keyfile(theme), GROUP))
        return TRUE;

    /* A user file replaces the system one entirely: start from it. */
    source = active_config_path();
    if (!(conf = lxp_libconfig_load(source ? source : target, TRUE, error)))
        return FALSE;

    for (gsize i = 0; i < G_N_ELEMENTS(settings); i++) {
        g_autofree char *value = lxp_theme_get_string(theme, GROUP, settings[i].theme_key);
        g_autofree char *norm = normalize(settings[i].type, value, settings[i].min,
                                          settings[i].max);
        if (norm)
            lxp_libconfig_set(conf, settings[i].picom_key, norm);
    }
    return lxp_libconfig_save(conf, target, error);
}

static gboolean compositor_reload(GError **error)
{
    g_autofree char *config = active_config_path();
    GPid pid = lxp_process_pid("picom");

    if (pid > 0) {
        kill(pid, SIGUSR1);  /* picom rereads its configuration */
        return TRUE;
    }
    if (config && g_str_has_prefix(config, g_get_user_config_dir())) {
        g_set_error_literal(error, LXP_ERROR, LXP_ERROR_FAILED,
                            "picom is not running, so rounded corners and shadows are "
                            "not shown (start it with “picom --daemon”)");
        return FALSE;
    }
    return TRUE;
}

const LxpBackend lxp_backend_compositor = {
    .id = "compositor",
    .description = "Rounded corners, shadows and fading (picom)",
    .available = compositor_available,
    .managed_files = compositor_managed_files,
    .check = compositor_check,
    .capture = compositor_capture,
    .apply = compositor_apply,
    .reload = compositor_reload,
};
