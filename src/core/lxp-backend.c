/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-backend.h"

#include "backends/backends.h"
#include "lxp-assets.h"
#include "lxp-system.h"
#include "lxp-error.h"

static const LxpBackend *const all_backends[] = {
    &lxp_backend_lxqt,
    &lxp_backend_openbox,
    &lxp_backend_gtk,
    &lxp_backend_cursor,
    &lxp_backend_wallpaper,
    &lxp_backend_panel,
    &lxp_backend_compositor,
    &lxp_backend_kvantum,
    &lxp_backend_terminal,
    NULL,
};

const LxpBackend *const *lxp_backends_get_all(void)
{
    return all_backends;
}

const LxpBackend *lxp_backend_find(const char *id)
{
    for (const LxpBackend *const *b = all_backends; *b; b++)
        if (g_str_equal((*b)->id, id))
            return *b;
    return NULL;
}

gboolean lxp_backend_is_available(const LxpBackend *backend)
{
    return !backend->available || backend->available();
}

char *lxp_backend_locate(const LxpTheme *theme, const char *kind, const char *name,
                         const char *probe)
{
    char *path = lxp_find_installed(kind, name, probe);
    return path ? path : lxp_assets_find(theme, kind, name, probe);
}

gboolean lxp_backend_require(char *path, const char *what, const char *name,
                             GError **error)
{
    if (!path) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_NOT_FOUND,
                    "%s “%s” is not installed", what, name);
        return FALSE;
    }
    g_free(path);
    return TRUE;
}

gboolean lxp_backend_selected(const LxpBackend *backend, const char *const *only)
{
    return !only || g_strv_contains(only, backend->id);
}

GPtrArray *lxp_backends_managed_files(const char *const *only)
{
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);

    for (const LxpBackend *const *b = all_backends; *b; b++) {
        g_autoptr(GPtrArray) own = NULL;

        if (!(*b)->managed_files || !lxp_backend_is_available(*b) ||
            !lxp_backend_selected(*b, only))
            continue;
        own = (*b)->managed_files();
        for (guint i = 0; i < own->len; i++) {
            const char *path = g_ptr_array_index(own, i);
            if (!g_ptr_array_find_with_equal_func(files, path, g_str_equal, NULL))
                g_ptr_array_add(files, g_strdup(path));
        }
    }
    return files;
}
