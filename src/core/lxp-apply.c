/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-apply.h"

#include "lxp-backend.h"
#include "lxp-error.h"
#include "lxp-snapshot.h"

static gboolean usable(const LxpBackend *backend, const char *const *only)
{
    return lxp_backend_selected(backend, only) && lxp_backend_is_available(backend);
}

static void prefix_error(GError **error, const LxpBackend *backend)
{
    g_prefix_error(error, "%s: ", backend->id);
}

gboolean lxp_check_theme(const LxpTheme *theme, const char *const *only,
                         GError **error)
{
    for (const LxpBackend *const *b = lxp_backends_get_all(); *b; b++) {
        if (!usable(*b, only) || !(*b)->check)
            continue;
        if (!(*b)->check(theme, error)) {
            prefix_error(error, *b);
            return FALSE;
        }
    }
    return TRUE;
}

char *lxp_apply_theme(const LxpTheme *theme, const char *const *only,
                      LxpApplyFlags flags, GError **error)
{
    g_autoptr(GPtrArray) files = NULL;
    g_autofree char *label = NULL;
    g_autofree char *id = NULL;

    if (!(flags & LXP_APPLY_SKIP_CHECKS) && !lxp_check_theme(theme, only, error))
        return NULL;

    files = lxp_backends_managed_files(only);
    label = g_strdup_printf("before applying %s", lxp_theme_get_name(theme));
    if (!(id = lxp_snapshot_create(label, files, error)))
        return NULL;
    /* Pruning failures only leave old snapshots behind. */
    lxp_snapshot_prune(LXP_SNAPSHOTS_KEPT, NULL);

    for (const LxpBackend *const *b = lxp_backends_get_all(); *b; b++) {
        GError *local = NULL;

        if (!usable(*b, only) || !(*b)->apply)
            continue;
        if ((*b)->apply(theme, &local))
            continue;

        g_propagate_prefixed_error(error, local, "%s: ", (*b)->id);
        local = NULL;
        if (!lxp_snapshot_restore(id, &local)) {
            g_prefix_error(error, "Configuration may be inconsistent "
                                  "(restoring snapshot %s failed: %s): ",
                           id, local->message);
            g_error_free(local);
        }
        return NULL;
    }
    return g_steal_pointer(&id);
}

gboolean lxp_reload(const char *const *only, GError **error)
{
    g_autoptr(GString) failures = NULL;

    for (const LxpBackend *const *b = lxp_backends_get_all(); *b; b++) {
        g_autoptr(GError) local = NULL;

        if (!usable(*b, only) || !(*b)->reload)
            continue;
        if ((*b)->reload(&local))
            continue;

        if (!failures)
            failures = g_string_new("Reload failed: ");
        else
            g_string_append(failures, "; ");
        g_string_append_printf(failures, "%s: %s", (*b)->id, local->message);
    }

    if (failures) {
        g_set_error_literal(error, LXP_ERROR, LXP_ERROR_FAILED, failures->str);
        return FALSE;
    }
    return TRUE;
}

gboolean lxp_rollback(const char *id, gboolean reload, GError **error)
{
    if (!lxp_snapshot_restore(id, error))
        return FALSE;
    return !reload || lxp_reload(NULL, error);
}
