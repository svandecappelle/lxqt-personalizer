/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-scheme.h"

/*
 * Palette helpers for building schemes:
 *  - colors from an image: its dominant hue for the background family and
 *    its most vivid well-represented color as the accent;
 *  - light/dark flip keeping the hues;
 *  - accent harmonies.
 * Only the four base colors are set; optional ones are reset to automatic.
 */

gboolean lxp_palette_from_image(const char *path, gboolean dark, LxpScheme *scheme);
void     lxp_palette_flip(LxpScheme *scheme);

typedef enum {
    LXP_HARMONY_COMPLEMENTARY,  /* opposite hue */
    LXP_HARMONY_ANALOGOUS,      /* neighbouring hue */
    LXP_HARMONY_TRIADIC,        /* a third of the way round */
} LxpHarmony;

LxpColor lxp_palette_harmony(const LxpColor *accent, LxpHarmony harmony);
