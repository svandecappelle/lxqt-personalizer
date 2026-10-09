/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-scheme.h"

/*
 * A terminal color scheme (qtermwidget/Konsole .colorscheme) matching a
 * scheme: its background and text, and the eight ANSI colors in normal
 * and intense variants (colors->ansi, overrides included).
 */
char *lxp_terminal_colorscheme(const LxpSchemeColors *colors, const char *description);

/*
 * The derived ANSI colors for <colors>, tuned for a dark or light
 * background: 0-7 normal, 8-15 bright.
 */
void  lxp_terminal_palette(const LxpSchemeColors *colors, LxpColor ansi[LXP_ANSI_COLORS]);
