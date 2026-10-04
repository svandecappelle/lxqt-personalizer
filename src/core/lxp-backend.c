/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-backend.h"

#include "backends/backends.h"

static const LxpBackend *const all_backends[] = {
    &lxp_backend_lxqt,
    &lxp_backend_openbox,
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

GPtrArray *lxp_backends_managed_files(void)
{
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);

    for (const LxpBackend *const *b = all_backends; *b; b++) {
        g_autoptr(GPtrArray) own = NULL;

        if (!(*b)->managed_files || !lxp_backend_is_available(*b))
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
