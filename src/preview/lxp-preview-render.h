/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-preview-model.h"

#include <cairo.h>

/* The mockup is laid out on a virtual screen of this size. */
#define LXP_PREVIEW_WIDTH  1440
#define LXP_PREVIEW_HEIGHT 900

/*
 * Draw a desktop mockup for <model> into <width> x <height> (user units
 * of <cr>), scaled to fit and centered. Main thread only (shared caches).
 */
void lxp_preview_render(cairo_t *cr, const LxpPreviewModel *model,
                        double width, double height);

/* Render to a new PNG file. */
gboolean lxp_preview_render_png(const LxpPreviewModel *model, int width,
                                int height, const char *path, GError **error);
