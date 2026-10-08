/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Kvantum widget theme: [General] theme in ~/.config/Kvantum/kvantum.kvconfig.
 * Only used when the Qt widget style is "kvantum". Running applications
 * keep their Kvantum theme until they are restarted.
 */
#include "backends.h"

#include "lxp-assets.h"
#include "lxp-catalog.h"
#include "lxp-error.h"
#include "lxp-ini.h"
#include "lxp-paths.h"

#define GROUP    "Kvantum"
#define KVCONFIG "Kvantum/kvantum.kvconfig"

static gboolean kvantum_available(void)
{
    g_autofree char *manager = g_find_program_in_path("kvantummanager");
    g_autoptr(GPtrArray) styles = NULL;

    if (manager)
        return TRUE;
    styles = lxp_catalog_list(LXP_CATALOG_QT_STYLES);
    return g_ptr_array_find_with_equal_func(styles, "kvantum", g_str_equal, NULL);
}

static GPtrArray *kvantum_managed_files(void)
{
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);

    g_ptr_array_add(files, lxp_paths_user_config(KVCONFIG));
    return files;
}

static gboolean kvantum_check(const LxpTheme *theme, GError **error)
{
    g_autofree char *name = lxp_theme_get_string(theme, GROUP, "theme");

    if (!name || g_str_equal(name, "Default"))
        return TRUE;
    {
        char *found = lxp_catalog_find_kvantum(name);
        if (!found)
            found = lxp_assets_find_config(theme, "Kvantum", name, NULL);
        return lxp_backend_require(found, "Kvantum theme", name, error);
    }
}

static gboolean kvantum_capture(LxpTheme *theme, GError **error)
{
    g_autofree char *path = lxp_paths_user_config(KVCONFIG);
    g_autoptr(LxpIni) ini = lxp_ini_load(path, TRUE, error);
    const char *name;

    if (!ini)
        return FALSE;
    lxp_theme_remove_group(theme, GROUP);
    if ((name = lxp_ini_get(ini, "General", "theme")) && *name)
        lxp_theme_set_string(theme, GROUP, "theme", name);
    return TRUE;
}

static gboolean kvantum_apply(const LxpTheme *theme, GError **error)
{
    g_autofree char *name = lxp_theme_get_string(theme, GROUP, "theme");
    g_autofree char *path = NULL;
    g_autoptr(LxpIni) ini = NULL;

    if (!name)
        return TRUE;
    path = lxp_paths_user_config(KVCONFIG);
    if (!(ini = lxp_ini_load(path, TRUE, error)))
        return FALSE;
    lxp_ini_set(ini, "General", "theme", name);
    return lxp_ini_save(ini, path, error);
}

const LxpBackend lxp_backend_kvantum = {
    .id = "kvantum",
    .description = "Kvantum widget theme (with the kvantum style)",
    .available = kvantum_available,
    .managed_files = kvantum_managed_files,
    .check = kvantum_check,
    .capture = kvantum_capture,
    .apply = kvantum_apply,
};
