/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-terminal-gen.h"

#include <math.h>

static void section(GString *out, const char *name, const LxpColor *c)
{
    g_string_append_printf(out, "[%s]\nBold=false\nColor=%d,%d,%d\n\n", name,
                           (int)round(CLAMP(c->r, 0, 1) * 255),
                           (int)round(CLAMP(c->g, 0, 1) * 255),
                           (int)round(CLAMP(c->b, 0, 1) * 255));
}

char *lxp_terminal_colorscheme(const LxpSchemeColors *c, const char *description)
{
    /* ANSI 1-6: red, green, yellow, blue, magenta, cyan */
    static const double hues[] = { 357, 135, 42, 215, 300, 185 };
    LxpColor black = { 0, 0, 0, 1 }, white = { 1, 1, 1, 1 };
    LxpColor bg = c->dark ? lxp_color_mix(&c->bg, &black, 0.25) : c->surface;
    LxpColor fg = c->fg;
    LxpColor fg_intense = c->dark ? lxp_color_mix(&fg, &white, 0.5) : lxp_color_mix(&fg, &black, 0.5);
    GString *out = g_string_new(NULL);
    double ah, as, al;

    lxp_color_to_hsl(&c->accent, &ah, &as, &al);
    section(out, "Background", &bg);
    {
        LxpColor bgi = lxp_color_mix(&bg, &fg, 0.08);
        section(out, "BackgroundIntense", &bgi);
    }

    for (int i = 0; i < 8; i++) {
        g_autofree char *name = g_strdup_printf("Color%d", i);
        g_autofree char *intense = g_strdup_printf("Color%dIntense", i);
        LxpColor normal, bright;

        if (i == 0) {
            normal = c->dark ? lxp_color_mix(&bg, &fg, 0.18) : lxp_color_mix(&fg, &bg, 0.15);
            bright = c->dark ? lxp_color_mix(&bg, &fg, 0.40) : lxp_color_mix(&fg, &bg, 0.40);
        } else if (i == 7) {
            normal = c->dark ? lxp_color_mix(&fg, &bg, 0.15) : lxp_color_mix(&bg, &fg, 0.30);
            bright = c->dark ? fg_intense : lxp_color_mix(&bg, &fg, 0.15);
        } else {
            /* standard hues, a touch pulled towards the accent */
            double hue = hues[i - 1];
            double delta = fmod(ah - hue + 540, 360) - 180;
            double sat = CLAMP(as, 0.45, 0.75);

            hue += delta * 0.12;
            normal = lxp_color_from_hsl(hue, sat, c->dark ? 0.62 : 0.40, 1);
            bright = lxp_color_from_hsl(hue, MIN(1, sat + 0.1), c->dark ? 0.74 : 0.30, 1);
        }
        section(out, name, &normal);
        section(out, intense, &bright);
    }

    section(out, "Foreground", &fg);
    section(out, "ForegroundIntense", &fg_intense);
    g_string_append_printf(out, "[General]\nDescription=%s\nOpacity=1\n", description);
    return g_string_free(out, FALSE);
}
