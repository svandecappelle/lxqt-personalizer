/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <cairo.h>
#include <glib.h>

/* Pixel size of an image file without decoding it. */
gboolean         lxp_image_get_size(const char *path, int *width, int *height);

/*
 * <path> decoded at <width> x <height> pixels (aspect not preserved; pass
 * the size you want drawn). SVG works through the gdk-pixbuf loader.
 * Cached; the cache owns the surface. NULL if it cannot be loaded.
 */
cairo_surface_t *lxp_image_load(const char *path, int width, int height);

void             lxp_image_cache_clear(void);
