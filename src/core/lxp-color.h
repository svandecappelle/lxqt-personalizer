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
