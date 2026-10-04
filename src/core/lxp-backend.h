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

    /* Store the current desktop state into <theme>. */
    gboolean   (*capture)(LxpTheme *theme, GError **error);

    /* Write the configuration described by <theme>. */
    gboolean   (*apply)(const LxpTheme *theme, GError **error);

    /* Ask running programs to pick up the new configuration. */
    gboolean   (*reload)(GError **error);
} LxpBackend;

/* NULL-terminated list of all backends. */
const LxpBackend *const *lxp_backends_get_all(void);
const LxpBackend        *lxp_backend_find(const char *id);
gboolean                 lxp_backend_is_available(const LxpBackend *backend);

/* Union of managed_files() of every available backend. */
GPtrArray               *lxp_backends_managed_files(void);
