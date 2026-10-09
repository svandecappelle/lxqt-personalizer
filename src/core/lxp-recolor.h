/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-color.h"

/*
 * Recolor a stylesheet to a new palette. Every color literal is mapped:
 *   - neutral colors (low saturation) along the source's
 *     view/background/foreground lightness axis onto the target's
 *     surface/background/foreground (piecewise linear, extrapolated);
 *   - colors close to the source accent hue onto the target accent,
 *     keeping their lightness offset from the source accent;
 *   - other colors (errors, warnings, success) unchanged.
 * Alpha is preserved.
 */
typedef struct {
    LxpColor background, surface, foreground, accent;
} LxpRecolorPalette;

LxpColor lxp_recolor_color(const LxpColor *color, const LxpRecolorPalette *from,
                           const LxpRecolorPalette *to);

/* Recolor #hex, rgb()/rgba() and white/black literals in CSS text. */
char    *lxp_recolor_css(const char *css, const LxpRecolorPalette *from,
                         const LxpRecolorPalette *to);

/*
 * Shift colors whose hue is within <range> degrees of <from> to the hue of
 * <to>, keeping their relative saturation and shading (icons: folder blue
 * to the accent). Neutral colors are kept.
 */
char    *lxp_recolor_hue(const char *text, const LxpColor *from, const LxpColor *to,
                         double range);

/*
 * Read the source palette from GTK's own "@define-color theme_bg_color"
 * (theme_base_color, theme_fg_color, theme_selected_bg_color) lines.
 */
gboolean lxp_recolor_palette_from_gtk_css(const char *css, LxpRecolorPalette *palette);
