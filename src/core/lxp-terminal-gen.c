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

static LxpColor terminal_bg(const LxpSchemeColors *c)
{
    LxpColor black = { 0, 0, 0, 1 };
    return c->dark ? lxp_color_mix(&c->bg, &black, 0.25) : c->surface;
}

static LxpColor terminal_fg_intense(const LxpSchemeColors *c)
{
    LxpColor black = { 0, 0, 0, 1 }, white = { 1, 1, 1, 1 };
    return c->dark ? lxp_color_mix(&c->fg, &white, 0.5) : lxp_color_mix(&c->fg, &black, 0.5);
}

void lxp_terminal_palette(const LxpSchemeColors *c, LxpColor ansi[LXP_ANSI_COLORS])
{
    /* ANSI 1-6: red, green, yellow, blue, magenta, cyan */
    static const double hues[] = { 357, 135, 42, 215, 300, 185 };
    LxpColor bg = terminal_bg(c);
    LxpColor fg = c->fg;
    double ah, as, al;

    lxp_color_to_hsl(&c->accent, &ah, &as, &al);
    for (int i = 0; i < 8; i++) {
        LxpColor *normal = &ansi[i], *bright = &ansi[i + 8];

        if (i == 0) {
            *normal = c->dark ? lxp_color_mix(&bg, &fg, 0.18) : lxp_color_mix(&fg, &bg, 0.15);
            *bright = c->dark ? lxp_color_mix(&bg, &fg, 0.40) : lxp_color_mix(&fg, &bg, 0.40);
        } else if (i == 7) {
            *normal = c->dark ? lxp_color_mix(&fg, &bg, 0.15) : lxp_color_mix(&bg, &fg, 0.30);
            *bright = c->dark ? terminal_fg_intense(c) : lxp_color_mix(&bg, &fg, 0.15);
        } else {
            /* standard hues, a touch pulled towards the accent */
            double hue = hues[i - 1];
            double delta = fmod(ah - hue + 540, 360) - 180;
            double sat = CLAMP(as, 0.45, 0.75);

            hue += delta * 0.12;
            *normal = lxp_color_from_hsl(hue, sat, c->dark ? 0.62 : 0.40, 1);
            *bright = lxp_color_from_hsl(hue, MIN(1, sat + 0.1), c->dark ? 0.74 : 0.30, 1);
        }
    }
}

char *lxp_terminal_colorscheme(const LxpSchemeColors *c, const char *description)
{
    LxpColor bg = terminal_bg(c);
    LxpColor fg_intense = terminal_fg_intense(c);
    LxpColor bgi = lxp_color_mix(&bg, &c->fg, 0.08);
    GString *out = g_string_new(NULL);

    section(out, "Background", &bg);
    section(out, "BackgroundIntense", &bgi);
    for (int i = 0; i < 8; i++) {
        g_autofree char *name = g_strdup_printf("Color%d", i);
        g_autofree char *intense = g_strdup_printf("Color%dIntense", i);

        section(out, name, &c->ansi[i]);
        section(out, intense, &c->ansi[i + 8]);
    }
    section(out, "Foreground", &c->fg);
    section(out, "ForegroundIntense", &fg_intense);
    g_string_append_printf(out, "[General]\nDescription=%s\nOpacity=1\n", description);
    return g_string_free(out, FALSE);
}
