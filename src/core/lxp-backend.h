/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-theme.h"

/*
 * A backend handles one part of the desktop (LXQt, Openbox, GTK...).
 * Optional callbacks may be NULL.
 */
typedef struct {
    const char *id;            /* "openbox" */
    const char *description;   /* human readable */

    /* Is the component installed? */
    gboolean   (*available)(void);

    /* User files that apply() may write; snapshotted before applying. */
    GPtrArray *(*managed_files)(void);

    /*
     * Verify that <theme> can be applied (referenced themes installed,
     * files present...). Called for every backend before anything is
     * written.
     */
    gboolean   (*check)(const LxpTheme *theme, GError **error);

    /* Store the current desktop state into <theme>. */
    gboolean   (*capture)(LxpTheme *theme, GError **error);

    /*
     * Write the configuration described by <theme>. Must leave files
     * untouched when the theme has no data for this backend.
     */
    gboolean   (*apply)(const LxpTheme *theme, GError **error);

    /*
     * Make running programs use the configuration currently on disk.
     * Must not depend on the theme, so it also works after a rollback.
     */
    gboolean   (*reload)(GError **error);
} LxpBackend;

/* NULL-terminated list of all backends. */
const LxpBackend *const *lxp_backends_get_all(void);
const LxpBackend        *lxp_backend_find(const char *id);
gboolean                 lxp_backend_is_available(const LxpBackend *backend);

/*
 * Directory of asset <name> of <kind> ("themes", "icons", "lxqt/themes")
 * containing <probe>: installed (lxp_find_installed) or bundled in the
 * theme (lxp_assets_find). NULL if neither.
 */
char                    *lxp_backend_locate(const LxpTheme *theme, const char *kind,
                                            const char *name, const char *probe);

/* Fail with a "not installed" error when <path> is NULL; frees <path>. */
gboolean                 lxp_backend_require(char *path, const char *what,
                                             const char *name, GError **error);

/* Is <backend> selected by <only> (backend ids; NULL selects all)? */
gboolean                 lxp_backend_selected(const LxpBackend *backend,
                                              const char *const *only);

/* Union of managed_files() of the available backends selected by <only>. */
GPtrArray               *lxp_backends_managed_files(const char *const *only);
