/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-scheme.h"

/*
 * A terminal color scheme (qtermwidget/Konsole .colorscheme) matching a
 * scheme: its background and text, and the eight ANSI colors in normal
 * and intense variants tuned for a dark or light background.
 */
char *lxp_terminal_colorscheme(const LxpSchemeColors *colors, const char *description);
