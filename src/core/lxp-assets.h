/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-theme.h"

/*
 * Themes can bundle installable data under <theme dir>/data/, laid out
 * like $XDG_DATA_HOME (data/themes/X/openbox-3/themerc,
 * data/lxqt/themes/X/...). Each installable directory contains a
 * ".lxp-asset" marker; it is copied to $XDG_DATA_HOME, replacing a
 * previous copy, but never over a directory without the marker (one the
 * user installed some other way).
 */

#define LXP_ASSET_MARKER ".lxp-asset"

gboolean lxp_assets_install(const LxpTheme *theme, GError **error);

/*
 * <theme dir>/data/<kind>/<name> if it exists and contains <probe>
 * (may be NULL), else NULL. Lets previews use assets before installing.
 */
char    *lxp_assets_find(const LxpTheme *theme, const char *kind, const char *name,
                         const char *probe);
