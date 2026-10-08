/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-color.h"

#include <math.h>
#include <string.h>

static const struct {
    const char *name;
    guint32 rgb;
} named[] = {
    { "black", 0x000000 },   { "white", 0xffffff },  { "red", 0xff0000 },
    { "green", 0x00ff00 },   { "blue", 0x0000ff },   { "yellow", 0xffff00 },
    { "cyan", 0x00ffff },    { "magenta", 0xff00ff }, { "orange", 0xffa500 },
    { "gray", 0xbebebe },    { "grey", 0xbebebe },   { "darkgray", 0xa9a9a9 },
    { "darkgrey", 0xa9a9a9 }, { "lightgray", 0xd3d3d3 },
    { "lightgrey", 0xd3d3d3 }, { "dimgray", 0x696969 }, { "dimgrey", 0x696969 },
    { "navy", 0x000080 },    { "steelblue", 0x4682b4 },
    { "slategray", 0x708090 }, { "slategrey", 0x708090 },
};

static void set_rgb(LxpColor *c, guint32 rgb)
{
    c->r = ((rgb >> 16) & 0xff) / 255.0;
    c->g = ((rgb >> 8) & 0xff) / 255.0;
    c->b = (rgb & 0xff) / 255.0;
    c->a = 1.0;
}

static gboolean parse_hex_digits(const char *s, size_t len, guint64 *out)
{
    *out = 0;
    if (len == 0)
        return FALSE;
    for (size_t i = 0; i < len; i++) {
        int v = g_ascii_xdigit_value(s[i]);
        if (v < 0)
            return FALSE;
        *out = *out * 16 + (guint64)v;
    }
    return TRUE;
}

static gboolean parse_hash(const char *s, LxpColor *c)
{
    size_t len = strlen(s);
    guint64 v;

    if (!parse_hex_digits(s, len, &v))
        return FALSE;
    switch (len) {
    case 3:
        c->r = ((v >> 8) & 0xf) / 15.0;
        c->g = ((v >> 4) & 0xf) / 15.0;
        c->b = (v & 0xf) / 15.0;
        c->a = 1.0;
        return TRUE;
    case 6:
        set_rgb(c, (guint32)v);
        return TRUE;
    case 8:
        set_rgb(c, (guint32)(v >> 8));
        c->a = (v & 0xff) / 255.0;
        return TRUE;
    default:
        return FALSE;
    }
}

/* rgb:R/G/B, each channel 1-4 hex digits scaled to its own width. */
static gboolean parse_x11_rgb(const char *s, LxpColor *c)
{
    g_auto(GStrv) parts = g_strsplit(s, "/", -1);
    double *channels[] = { &c->r, &c->g, &c->b };

    if (g_strv_length(parts) != 3)
        return FALSE;
    for (int i = 0; i < 3; i++) {
        size_t len = strlen(parts[i]);
        guint64 v;

        if (len > 4 || !parse_hex_digits(parts[i], len, &v))
            return FALSE;
        *channels[i] = (double)v / (double)((1u << (4 * len)) - 1);
    }
    c->a = 1.0;
    return TRUE;
}

gboolean lxp_color_parse(const char *text, LxpColor *color)
{
    g_autofree char *copy = NULL;
    g_autofree char *s = NULL;
    guint64 level;

    if (!text)
        return FALSE;
    copy = g_strdup(text);
    s = g_ascii_strdown(g_strstrip(copy), -1);

    if (s[0] == '#')
        return parse_hash(s + 1, color);
    if (g_str_has_prefix(s, "rgb:"))
        return parse_x11_rgb(s + 4, color);

    /* gray0 .. gray100 */
    if ((g_str_has_prefix(s, "gray") || g_str_has_prefix(s, "grey")) &&
        g_ascii_isdigit(s[4]) &&
        g_ascii_string_to_unsigned(s + 4, 10, 0, 100, &level, NULL)) {
        color->r = color->g = color->b = round(level * 255.0 / 100.0) / 255.0;
        color->a = 1.0;
        return TRUE;
    }

    for (gsize i = 0; i < G_N_ELEMENTS(named); i++) {
        if (g_str_equal(s, named[i].name)) {
            set_rgb(color, named[i].rgb);
            return TRUE;
        }
    }
    return FALSE;
}

/* Decode the QSettings byte-array escaping: \0, \xNN, \\ and literals. */
static GByteArray *qt_unescape_bytes(const char *s, const char *end)
{
    GByteArray *bytes = g_byte_array_new();

    while (s < end) {
        guint8 b;

        if (*s != '\\' || s + 1 >= end) {
            b = (guint8)*s++;
        } else if (s[1] == 'x') {
            int v = 0, n = 0;
            s += 2;
            while (n < 2 && s < end && g_ascii_isxdigit(*s)) {
                v = v * 16 + g_ascii_xdigit_value(*s++);
                n++;
            }
            b = (guint8)v;
        } else if (s[1] == '0') {
            b = 0;
            s += 2;
        } else {
            b = (guint8)s[1];
            s += 2;
        }
        g_byte_array_append(bytes, &b, 1);
    }
    return bytes;
}

static guint16 be16(const guint8 *p)
{
    return (guint16)((p[0] << 8) | p[1]);
}

gboolean lxp_qt_color_parse(const char *raw, LxpColor *color)
{
    const char *start, *end;
    g_autoptr(GByteArray) bytes = NULL;
    const guint8 *d;

    if (!raw)
        return FALSE;
    if (!g_str_has_prefix(raw, "@Variant("))
        return raw[0] == '#' && lxp_color_parse(raw, color);

    start = raw + strlen("@Variant(");
    end = strrchr(start, ')');
    if (!end)
        return FALSE;

    /* quint32 type (67 = QColor), qint8 spec, quint16 alpha, r, g, b, pad */
    bytes = qt_unescape_bytes(start, end);
    if (bytes->len < 4 + 1 + 5 * 2)
        return FALSE;
    d = bytes->data;
    if (d[0] || d[1] || d[2] || d[3] != 0x43)
        return FALSE;
    if (d[4] != 1)  /* only QColor::Rgb; 0 means invalid/default */
        return FALSE;

    color->a = be16(d + 5) / 65535.0;
    color->r = be16(d + 7) / 65535.0;
    color->g = be16(d + 9) / 65535.0;
    color->b = be16(d + 11) / 65535.0;
    return TRUE;
}

char *lxp_color_to_hex(const LxpColor *c)
{
    return g_strdup_printf("#%02x%02x%02x",
                           (int)round(CLAMP(c->r, 0, 1) * 255),
                           (int)round(CLAMP(c->g, 0, 1) * 255),
                           (int)round(CLAMP(c->b, 0, 1) * 255));
}

LxpColor lxp_color_mix(const LxpColor *a, const LxpColor *b, double t)
{
    LxpColor c = {
        a->r + (b->r - a->r) * t,
        a->g + (b->g - a->g) * t,
        a->b + (b->b - a->b) * t,
        a->a + (b->a - a->a) * t,
    };
    return c;
}

static double linear(double v)
{
    return v <= 0.03928 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

double lxp_color_luminance(const LxpColor *c)
{
    return 0.2126 * linear(c->r) + 0.7152 * linear(c->g) + 0.0722 * linear(c->b);
}

double lxp_color_contrast(const LxpColor *a, const LxpColor *b)
{
    double la = lxp_color_luminance(a) + 0.05;
    double lb = lxp_color_luminance(b) + 0.05;

    return la > lb ? la / lb : lb / la;
}

void lxp_color_to_hsl(const LxpColor *c, double *h, double *s, double *l)
{
    double max = MAX(c->r, MAX(c->g, c->b));
    double min = MIN(c->r, MIN(c->g, c->b));
    double d = max - min;

    *l = (max + min) / 2;
    if (d < 1e-9) {
        *h = 0;
        *s = 0;
        return;
    }
    *s = *l > 0.5 ? d / (2 - max - min) : d / (max + min);
    if (max == c->r)
        *h = fmod((c->g - c->b) / d + 6, 6);
    else if (max == c->g)
        *h = (c->b - c->r) / d + 2;
    else
        *h = (c->r - c->g) / d + 4;
    *h *= 60;
}

static double hue_channel(double p, double q, double t)
{
    if (t < 0)
        t += 1;
    if (t > 1)
        t -= 1;
    if (t < 1.0 / 6)
        return p + (q - p) * 6 * t;
    if (t < 0.5)
        return q;
    if (t < 2.0 / 3)
        return p + (q - p) * (2.0 / 3 - t) * 6;
    return p;
}

LxpColor lxp_color_from_hsl(double h, double s, double l, double alpha)
{
    LxpColor c = { l, l, l, alpha };
    double q, p, hk;

    s = CLAMP(s, 0, 1);
    l = CLAMP(l, 0, 1);
    if (s < 1e-9) {
        c.r = c.g = c.b = l;
        return c;
    }
    q = l < 0.5 ? l * (1 + s) : l + s - l * s;
    p = 2 * l - q;
    hk = fmod(fmod(h, 360) + 360, 360) / 360;
    c.r = hue_channel(p, q, hk + 1.0 / 3);
    c.g = hue_channel(p, q, hk);
    c.b = hue_channel(p, q, hk - 1.0 / 3);
    return c;
}

char *lxp_color_to_rgb_triplet(const LxpColor *c)
{
    return g_strdup_printf("%d, %d, %d",
                           (int)round(CLAMP(c->r, 0, 1) * 255),
                           (int)round(CLAMP(c->g, 0, 1) * 255),
                           (int)round(CLAMP(c->b, 0, 1) * 255));
}
