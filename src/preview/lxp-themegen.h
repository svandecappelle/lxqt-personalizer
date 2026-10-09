/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-theme.h"

#define LXP_WALLPAPER_WIDTH     2560
#define LXP_WALLPAPER_HEIGHT    1440
#define LXP_GENERATED_WALLPAPER "wallpaper-generated.png"

#define LXP_VARIANT_DIR "variant"

/*
 * Generate everything a theme with a [Scheme] needs into <dir>: the
 * Openbox/GTK/LXQt themes (lxp_scheme_generate) and, when the scheme names
 * a wallpaper style, the wallpaper (rendered at <width> x <height>; only
 * again when its inputs change). With "variant=true" in [Scheme], the
 * opposite light/dark version is generated too, as a complete theme in
 * <dir>/variant.
 */
gboolean lxp_theme_generate(LxpTheme *theme, const char *dir, int width, int height,
                            GError **error);

/*
 * A directory <dir>'s theme can be used from: <dir> itself, or for a
 * built-in theme (read-only, never generated) a copy in the cache,
 * generated on first use and again whenever the built-in changes.
 */
char    *lxp_theme_materialize(const char *dir, GError **error);

/* <dir>/variant when the theme has a light/dark variant, else NULL. */
char    *lxp_theme_variant_dir(const char *dir);
