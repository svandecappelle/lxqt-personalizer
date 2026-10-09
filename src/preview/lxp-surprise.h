/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-theme.h"

/*
 * Fill <theme> with a random but harmonious design: a dark or light
 * palette in one hue family with an accent chosen by color harmony, and
 * random window buttons, title bars, panel, wallpaper, corners and
 * effects. Uses the kvantum widget style only when <kvantum> is TRUE.
 * Returns a two-word name for it ("Velvet Dusk").
 */
char *lxp_surprise(LxpTheme *theme, GRand *rand, gboolean kvantum);
