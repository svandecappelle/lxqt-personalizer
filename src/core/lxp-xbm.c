/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-xbm.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SIZE 256

static int stride(int width)
{
    return (width + 7) / 8;
}

LxpXbm *lxp_xbm_new(int width, int height)
{
    LxpXbm *xbm = g_new0(LxpXbm, 1);

    xbm->width = CLAMP(width, 1, MAX_SIZE);
    xbm->height = CLAMP(height, 1, MAX_SIZE);
    xbm->bits = g_new0(guint8, stride(xbm->width) * xbm->height);
    return xbm;
}

void lxp_xbm_free(LxpXbm *xbm)
{
    if (!xbm)
        return;
    g_free(xbm->bits);
    g_free(xbm);
}

/* Value of "#define <anything>_<suffix> N". */
static int define_value(const char *text, const char *suffix)
{
    g_autofree char *needle = g_strconcat(suffix, " ", NULL);
    const char *p = text;

    while ((p = strstr(p, "#define"))) {
        const char *line_end = strchr(p, '\n');
        const char *hit = strstr(p, needle);

        if (hit && (!line_end || hit < line_end))
            return atoi(hit + strlen(needle));
        p += 7;
    }
    return 0;
}

LxpXbm *lxp_xbm_parse(const char *text)
{
    int width = define_value(text, "_width");
    int height = define_value(text, "_height");
    const char *p = text ? strchr(text, '{') : NULL;
    LxpXbm *xbm;
    gsize n = 0, total;

    if (width <= 0 || height <= 0 || width > MAX_SIZE || height > MAX_SIZE || !p)
        return NULL;
    xbm = lxp_xbm_new(width, height);
    total = (gsize)stride(width) * height;
    for (p++; *p && *p != '}' && n < total;) {
        char *end;
        unsigned long v;

        while (*p && (*p == ',' || g_ascii_isspace(*p)))
            p++;
        if (*p == '}' || !*p)
            break;
        v = strtoul(p, &end, 0);
        if (end == p) {
            lxp_xbm_free(xbm);
            return NULL;
        }
        xbm->bits[n++] = (guint8)v;
        p = end;
    }
    return xbm;
}

LxpXbm *lxp_xbm_load(const char *path)
{
    g_autofree char *text = NULL;

    if (!g_file_get_contents(path, &text, NULL, NULL))
        return NULL;
    return lxp_xbm_parse(text);
}

gboolean lxp_xbm_get(const LxpXbm *xbm, int x, int y)
{
    if (x < 0 || y < 0 || x >= xbm->width || y >= xbm->height)
        return FALSE;
    return (xbm->bits[y * stride(xbm->width) + x / 8] >> (x % 8)) & 1;
}

void lxp_xbm_set(LxpXbm *xbm, int x, int y)
{
    if (x < 0 || y < 0 || x >= xbm->width || y >= xbm->height)
        return;
    xbm->bits[y * stride(xbm->width) + x / 8] |= (guint8)(1u << (x % 8));
}

char *lxp_xbm_to_text(const LxpXbm *xbm, const char *name)
{
    GString *out = g_string_new(NULL);
    gsize total = (gsize)stride(xbm->width) * xbm->height;

    g_string_append_printf(out, "#define %s_width %d\n#define %s_height %d\n"
                                "static unsigned char %s_bits[] = {\n  ",
                           name, xbm->width, name, xbm->height, name);
    for (gsize i = 0; i < total; i++)
        g_string_append_printf(out, "0x%02x%s", xbm->bits[i],
                               i + 1 == total ? " };\n" : (i + 1) % 12 ? ", " : ",\n  ");
    return g_string_free(out, FALSE);
}

/* Pixel centers are at x + 0.5. */
void lxp_xbm_fill_circle(LxpXbm *xbm, double cx, double cy, double r)
{
    for (int y = 0; y < xbm->height; y++)
        for (int x = 0; x < xbm->width; x++)
            if (hypot(x + 0.5 - cx, y + 0.5 - cy) <= r)
                lxp_xbm_set(xbm, x, y);
}

void lxp_xbm_line(LxpXbm *xbm, double x0, double y0, double x1, double y1, double width)
{
    double dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;

    for (int y = 0; y < xbm->height; y++) {
        for (int x = 0; x < xbm->width; x++) {
            double px = x + 0.5, py = y + 0.5;
            double t = len2 > 0 ? CLAMP(((px - x0) * dx + (py - y0) * dy) / len2, 0, 1) : 0;
            if (hypot(px - (x0 + t * dx), py - (y0 + t * dy)) <= width / 2)
                lxp_xbm_set(xbm, x, y);
        }
    }
}

void lxp_xbm_rect(LxpXbm *xbm, int x, int y, int w, int h, int line)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            if (i < x + line || i >= x + w - line || j < y + line || j >= y + h - line)
                lxp_xbm_set(xbm, i, j);
}
