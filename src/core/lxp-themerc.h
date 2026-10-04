/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-color.h"

/*
 * Openbox theme file (openbox-3/themerc): X resource syntax
 * "pattern: value" where '*' matches any number of components and '?'
 * exactly one. Lookups follow X resource precedence: at each component,
 * from left to right, a literal match beats '?' which beats a skip.
 */

typedef struct _LxpThemerc LxpThemerc;

LxpThemerc *lxp_themerc_new_from_data(const char *data);
LxpThemerc *lxp_themerc_load(const char *path, GError **error);
/* Load openbox-3/themerc of installed Openbox theme <name>. */
LxpThemerc *lxp_themerc_load_theme(const char *name, GError **error);
void        lxp_themerc_free(LxpThemerc *rc);

/* Directory containing the themerc, or NULL. */
const char *lxp_themerc_get_dir(const LxpThemerc *rc);

/* Value for the fully qualified <key>, or NULL. */
const char *lxp_themerc_lookup(const LxpThemerc *rc, const char *key);

/* First key of the NULL-terminated list that resolves to a color. */
gboolean    lxp_themerc_color(const LxpThemerc *rc, LxpColor *color,
                              const char *key, ...) G_GNUC_NULL_TERMINATED;
int         lxp_themerc_int(const LxpThemerc *rc, const char *key, int fallback);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(LxpThemerc, lxp_themerc_free)
