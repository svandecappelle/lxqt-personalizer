/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-theme.h"

#define LXP_WALLPAPER_WIDTH     2560
#define LXP_WALLPAPER_HEIGHT    1440
#define LXP_GENERATED_WALLPAPER "wallpaper-generated.png"

/*
 * Generate everything a theme with a [Scheme] needs into <dir>: the
 * Openbox/GTK/LXQt themes (lxp_scheme_generate) and, when the scheme names
 * a wallpaper style, the wallpaper (rendered at <width> x <height>; only
 * again when its inputs change).
 */
gboolean lxp_theme_generate(LxpTheme *theme, const char *dir, int width, int height,
                            GError **error);

/*
 * A directory <dir>'s theme can be used from: <dir> itself, or for a
 * built-in theme (read-only, never generated) a copy in the cache,
 * generated on first use and again whenever the built-in changes.
 */
char    *lxp_theme_materialize(const char *dir, GError **error);
