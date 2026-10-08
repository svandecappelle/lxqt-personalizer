/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-theme.h"

/*
 * Applying a theme:
 *   0. install   assets bundled in the theme's data/ (lxp-assets.h)
 *   1. check()   every selected backend; no config is written on failure
 *   2. snapshot  the files the selected backends manage
 *   3. apply()   each backend; on failure the snapshot is restored
 * Reloading running programs is a separate step (lxp_reload), so callers
 * can apply, reload, and still roll back with lxp_rollback().
 *
 * <only> restricts the operation to these backend ids (NULL: all).
 */

typedef enum {
    LXP_APPLY_DEFAULT     = 0,
    LXP_APPLY_SKIP_CHECKS = 1 << 0,
} LxpApplyFlags;

/*
 * Capture the current desktop into a new, unsaved theme named <name>.
 * Backends that fail are skipped; their errors are appended to
 * <failures> (GError*, may be NULL).
 */
LxpTheme *lxp_capture_current(const char *name, const char *const *only,
                              GPtrArray *failures);

/* Verify that <theme> can be applied. */
gboolean lxp_check_theme(const LxpTheme *theme, const char *const *only,
                         GError **error);

/* Returns the id of the snapshot taken before applying. */
char    *lxp_apply_theme(const LxpTheme *theme, const char *const *only,
                         LxpApplyFlags flags, GError **error);

/*
 * Reload every selected backend, even when one fails. The error lists
 * all failures.
 */
gboolean lxp_reload(const char *const *only, GError **error);

/* Restore snapshot <id> (NULL: latest) and optionally reload. */
gboolean lxp_rollback(const char *id, gboolean reload, GError **error);
