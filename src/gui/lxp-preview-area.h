/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-preview-model.h"

#include <gtk/gtk.h>

/*
 * A GtkDrawingArea showing the desktop mockup of a theme. The rendering
 * is cached per size, so scrolling a gallery stays cheap.
 */
GtkWidget *lxp_preview_area_new(int width, int height);
/* Takes ownership of <model> (may be NULL to show nothing). */
void       lxp_preview_area_set_model(GtkWidget *area, LxpPreviewModel *model);
