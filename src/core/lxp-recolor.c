/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-recolor.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NEUTRAL_SATURATION 0.18
#define ACCENT_HUE_RANGE   35.0

#define SURFACE_RANGE      0.03

/* Unlike g_str_has_prefix(), does not measure the rest of the string. */
static gboolean starts_with(const char *p, const char *prefix)
{
    return strncmp(p, prefix, strlen(prefix)) == 0;
}

static double lightness(const LxpColor *c)
{
    double h, s, l;
    lxp_color_to_hsl(c, &h, &s, &l);
    return l;
}

static LxpColor shift_lightness(const LxpColor *c, double dl)
{
    double h, s, l;
    lxp_color_to_hsl(c, &h, &s, &l);
    return lxp_color_from_hsl(h, s, l + dl, 1);
}

static LxpColor clamp_color(LxpColor c)
{
    c.r = CLAMP(c.r, 0, 1);
    c.g = CLAMP(c.g, 0, 1);
    c.b = CLAMP(c.b, 0, 1);
    return c;
}

/*
 * Neutral colors: close to the source view color -> target surface (same
 * small offset); otherwise along the background -> foreground axis, and
 * beyond the background (away from the text) proportionally towards
 * black (dark themes) or white (light themes).
 */
static LxpColor map_neutral(double l, const LxpRecolorPalette *from,
                            const LxpRecolorPalette *to)
{
    double lb = lightness(&from->background);
    double lf = lightness(&from->foreground);
    double ls = lightness(&from->surface);
    gboolean dark = lightness(&to->foreground) > lightness(&to->background);
    LxpColor away = { dark ? 0 : 1, dark ? 0 : 1, dark ? 0 : 1, 1 };
    double t, room, s;

    if (fabs(l - ls) <= SURFACE_RANGE && fabs(l - ls) < fabs(l - lb))
        return shift_lightness(&to->surface, l - ls);

    if (fabs(lf - lb) < 1e-6)
        return to->background;
    t = (l - lb) / (lf - lb);
    if (t >= 0)
        return clamp_color(lxp_color_mix(&to->background, &to->foreground, t));

    room = lf > lb ? lb : 1 - lb;
    s = room > 1e-6 ? MIN(1, fabs(l - lb) / room) : 0;
    return lxp_color_mix(&to->background, &away, s);
}

static double hue_distance(double a, double b)
{
    double d = fabs(a - b);
    return d > 180 ? 360 - d : d;
}

LxpColor lxp_recolor_color(const LxpColor *c, const LxpRecolorPalette *from,
                           const LxpRecolorPalette *to)
{
    double h, s, l, ah, as, al, th, ts, tl;
    LxpColor out;

    lxp_color_to_hsl(c, &h, &s, &l);
    lxp_color_to_hsl(&from->accent, &ah, &as, &al);

    if (s < NEUTRAL_SATURATION) {
        out = map_neutral(l, from, to);
    } else if (hue_distance(h, ah) <= ACCENT_HUE_RANGE) {
        lxp_color_to_hsl(&to->accent, &th, &ts, &tl);
        out = lxp_color_from_hsl(th, as > 0 ? ts * (s / as) : ts, tl + (l - al), 1);
    } else {
        out = *c;
    }
    out.a = c->a;
    return out;
}

static void append_color(GString *out, const LxpColor *c, gboolean function)
{
    int r = (int)round(c->r * 255), g = (int)round(c->g * 255), b = (int)round(c->b * 255);

    if (function || c->a < 1.0) {
        char alpha[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_formatd(alpha, sizeof(alpha), "%.3g", c->a);
        g_string_append_printf(out, "rgba(%d, %d, %d, %s)", r, g, b, alpha);
    } else {
        g_string_append_printf(out, "#%02x%02x%02x", r, g, b);
    }
}

/* Parse "rgb(r, g, b)" / "rgba(r, g, b, a)" at <p>; returns chars consumed. */
static size_t parse_rgb_function(const char *p, LxpColor *c)
{
    gboolean has_alpha = starts_with(p, "rgba(");
    const char *start = p + (has_alpha ? 5 : 4);
    const char *end = strchr(start, ')');
    g_autofree char *inner = NULL;
    g_auto(GStrv) parts = NULL;
    guint n;

    if (!end)
        return 0;
    inner = g_strndup(start, end - start);
    parts = g_strsplit(inner, ",", -1);
    n = g_strv_length(parts);
    if (n != (has_alpha ? 4u : 3u))
        return 0;
    for (guint i = 0; i < n; i++) {
        char *e;
        double v;

        g_strstrip(parts[i]);
        v = g_ascii_strtod(parts[i], &e);
        if (e == parts[i] || (*e && *e != '%'))
            return 0;
        if (i < 3)
            (&c->r)[i] = CLAMP((*e == '%' ? v / 100 : v / 255), 0, 1);
        else
            c->a = CLAMP(v, 0, 1);
    }
    if (!has_alpha)
        c->a = 1;
    return (size_t)(end - p + 1);
}

static gboolean is_ident_char(char ch)
{
    return g_ascii_isalnum(ch) || ch == '-' || ch == '_';
}

typedef LxpColor (*MapColor)(const LxpColor *color, gconstpointer data);

typedef struct {
    const LxpRecolorPalette *from, *to;
} PaletteMap;

static LxpColor map_palette(const LxpColor *c, gconstpointer data)
{
    const PaletteMap *pm = data;
    return lxp_recolor_color(c, pm->from, pm->to);
}

typedef struct {
    double from_h, from_s, from_l;
    double to_h, to_s, to_l;
    double range;
} HueMap;

/* Colors near the source hue take the target hue; shading is kept. */
static LxpColor map_hue(const LxpColor *c, gconstpointer data)
{
    const HueMap *hm = data;
    double h, s, l, d;
    LxpColor out;

    lxp_color_to_hsl(c, &h, &s, &l);
    d = fabs(h - hm->from_h);
    if (d > 180)
        d = 360 - d;
    if (s < NEUTRAL_SATURATION || d > hm->range)
        return *c;
    out = lxp_color_from_hsl(hm->to_h + (h - hm->from_h),
                             hm->from_s > 0 ? s * hm->to_s / hm->from_s : hm->to_s,
                             l + (hm->to_l - hm->from_l) * 0.7, 1);
    out.a = c->a;
    return out;
}

static char *recolor_text(const char *css, MapColor map, gconstpointer data);

char *lxp_recolor_css(const char *css, const LxpRecolorPalette *from,
                      const LxpRecolorPalette *to)
{
    PaletteMap pm = { from, to };
    return recolor_text(css, map_palette, &pm);
}

char *lxp_recolor_hue(const char *text, const LxpColor *from, const LxpColor *to,
                      double range)
{
    HueMap hm;

    lxp_color_to_hsl(from, &hm.from_h, &hm.from_s, &hm.from_l);
    lxp_color_to_hsl(to, &hm.to_h, &hm.to_s, &hm.to_l);
    hm.range = range;
    return recolor_text(text, map_hue, &hm);
}

static char *recolor_text(const char *css, MapColor map, gconstpointer data)
{
    GString *out = g_string_sized_new(strlen(css));
    const char *p = css;
    gboolean in_comment = FALSE;

    while (*p) {
        LxpColor c, mapped;
        size_t len = 0;
        gboolean function = FALSE;

        if (in_comment) {
            if (p[0] == '*' && p[1] == '/') {
                g_string_append(out, "*/");
                p += 2;
                in_comment = FALSE;
            } else {
                g_string_append_c(out, *p++);
            }
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            in_comment = TRUE;
            g_string_append(out, "/*");
            p += 2;
            continue;
        }

        if (*p == '#' && (p == css || !is_ident_char(p[-1]))) {
            while (g_ascii_isxdigit(p[1 + len]))
                len++;
            if ((len == 3 || len == 6 || len == 8) && !is_ident_char(p[1 + len])) {
                g_autofree char *hex = g_strndup(p, len + 1);
                if (lxp_color_parse(hex, &c))
                    len += 1;
                else
                    len = 0;
            } else {
                len = 0;
            }
        } else if ((starts_with(p, "rgb(") || starts_with(p, "rgba(")) &&
                   (p == css || !is_ident_char(p[-1]))) {
            len = parse_rgb_function(p, &c);
            function = TRUE;
        } else if ((starts_with(p, "white") || starts_with(p, "black")) &&
                   (p == css || !is_ident_char(p[-1])) && !is_ident_char(p[5])) {
            c.r = c.g = c.b = p[0] == 'w' ? 1 : 0;
            c.a = 1;
            len = 5;
        }

        if (len == 0) {
            g_string_append_c(out, *p++);
            continue;
        }
        mapped = map(&c, data);
        append_color(out, &mapped, function);
        p += len;
    }
    return g_string_free(out, FALSE);
}

static gboolean find_define(const char *css, const char *name, LxpColor *c)
{
    g_autofree char *needle = g_strconcat("@define-color ", name, " ", NULL);
    const char *p = strstr(css, needle);
    const char *end;
    g_autofree char *value = NULL;

    if (!p)
        return FALSE;
    p += strlen(needle);
    end = strchr(p, ';');
    if (!end)
        return FALSE;
    value = g_strstrip(g_strndup(p, end - p));
    if (g_str_has_prefix(value, "rgb"))
        return parse_rgb_function(value, c) > 0;
    return lxp_color_parse(value, c);
}

gboolean lxp_recolor_palette_from_gtk_css(const char *css, LxpRecolorPalette *palette)
{
    return find_define(css, "theme_bg_color", &palette->background) &&
           find_define(css, "theme_base_color", &palette->surface) &&
           find_define(css, "theme_fg_color", &palette->foreground) &&
           find_define(css, "theme_selected_bg_color", &palette->accent);
}
