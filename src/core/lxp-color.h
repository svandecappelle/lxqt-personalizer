/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <glib.h>

/* Components are in [0, 1]. */
typedef struct {
    double r, g, b, a;
} LxpColor;

/*
 * Parse "#rgb", "#rrggbb", "#rrggbbaa", X11 "rgb:r/g/b" (1-4 hex digits
 * per channel) and common X11 color names ("white", "gray40"...).
 */
gboolean lxp_color_parse(const char *text, LxpColor *color);

/*
 * Parse a QSettings color: "#rrggbb" or a serialized QColor
 * "@Variant(\0\0\0\x43\x1\xff\xff...)". Returns FALSE for invalid colors
 * (QColor spec "Invalid", meaning "use the default").
 */
gboolean lxp_qt_color_parse(const char *raw, LxpColor *color);

/* "#rrggbb" (alpha ignored). */
char    *lxp_color_to_hex(const LxpColor *color);

LxpColor lxp_color_mix(const LxpColor *a, const LxpColor *b, double t);
/* Relative luminance (WCAG), 0 = black, 1 = white. */
double   lxp_color_luminance(const LxpColor *color);

/* WCAG contrast ratio, 1 (none) to 21 (black on white). */
double   lxp_color_contrast(const LxpColor *a, const LxpColor *b);

/* Hue in degrees [0, 360), saturation and lightness in [0, 1]. */
void     lxp_color_to_hsl(const LxpColor *color, double *h, double *s, double *l);
LxpColor lxp_color_from_hsl(double h, double s, double l, double alpha);

/* "r, g, b" with 0-255 components, for rgba() in CSS and QSS. */
char    *lxp_color_to_rgb_triplet(const LxpColor *color);
