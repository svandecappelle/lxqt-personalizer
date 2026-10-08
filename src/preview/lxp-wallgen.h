/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-scheme.h"

#include <cairo.h>

/*
 * Wallpapers drawn from a theme's colors, so themes need no image files
 * and always match their palette. <seed> picks a variation.
 *
 *   gradient   diagonal gradient with a soft accent glow
 *   blobs      large soft color fields (mesh-gradient look)
 *   waves      layered flowing bands rising from the bottom
 *   geometric  low-poly triangles shading across the screen
 */

extern const char *const lxp_wallgen_styles[];  /* NULL-terminated */

gboolean lxp_wallgen_is_style(const char *style);

void     lxp_wallgen_draw(cairo_t *cr, const LxpSchemeColors *colors, const char *style,
                          guint32 seed, int width, int height);

gboolean lxp_wallgen_render_png(const LxpSchemeColors *colors, const char *style,
                                guint32 seed, int width, int height, const char *path,
                                GError **error);
