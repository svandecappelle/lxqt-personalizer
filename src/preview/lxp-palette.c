/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-palette.h"

#include "lxp-image.h"

#include <math.h>

#define SAMPLE_W  96
#define SAMPLE_H  54
#define CLUSTERS  6
#define ROUNDS    12

typedef struct {
    double r, g, b;
    int    count;
} Cluster;

static double dist2(double r1, double g1, double b1, const Cluster *c)
{
    return (r1 - c->r) * (r1 - c->r) + (g1 - c->g) * (g1 - c->g) + (b1 - c->b) * (b1 - c->b);
}

/* k-means over the pixels of a small copy of the image. */
static gboolean cluster_image(const char *path, Cluster *clusters)
{
    cairo_surface_t *surface = lxp_image_load(path, SAMPLE_W, SAMPLE_H);
    int stride, n = SAMPLE_W * SAMPLE_H;
    const guint8 *data;
    g_autofree double *px = NULL;

    if (!surface)
        return FALSE;
    cairo_surface_flush(surface);
    data = cairo_image_surface_get_data(surface);
    stride = cairo_image_surface_get_stride(surface);
    px = g_new(double, 3 * n);
    for (int y = 0; y < SAMPLE_H; y++) {
        for (int x = 0; x < SAMPLE_W; x++) {
            guint32 v = ((const guint32 *)(data + y * stride))[x];
            double *p = px + 3 * (y * SAMPLE_W + x);
            p[0] = ((v >> 16) & 0xff) / 255.0;
            p[1] = ((v >> 8) & 0xff) / 255.0;
            p[2] = (v & 0xff) / 255.0;
        }
    }

    /* deterministic start: spread over the image */
    for (int k = 0; k < CLUSTERS; k++) {
        const double *p = px + 3 * ((k * 2 + 1) * n / (2 * CLUSTERS));
        clusters[k] = (Cluster){ p[0], p[1], p[2], 0 };
    }
    for (int round = 0; round < ROUNDS; round++) {
        double sum[CLUSTERS][3] = { { 0 } };

        for (int k = 0; k < CLUSTERS; k++)
            clusters[k].count = 0;
        for (int i = 0; i < n; i++) {
            const double *p = px + 3 * i;
            int best = 0;
            for (int k = 1; k < CLUSTERS; k++)
                if (dist2(p[0], p[1], p[2], &clusters[k]) < dist2(p[0], p[1], p[2], &clusters[best]))
                    best = k;
            clusters[best].count++;
            sum[best][0] += p[0];
            sum[best][1] += p[1];
            sum[best][2] += p[2];
        }
        for (int k = 0; k < CLUSTERS; k++) {
            if (!clusters[k].count)
                continue;
            clusters[k].r = sum[k][0] / clusters[k].count;
            clusters[k].g = sum[k][1] / clusters[k].count;
            clusters[k].b = sum[k][2] / clusters[k].count;
        }
    }
    return TRUE;
}

static void reset_optional(LxpScheme *s)
{
    s->has_accent_text = s->has_title = s->has_panel = s->has_link = FALSE;
}

gboolean lxp_palette_from_image(const char *path, gboolean dark, LxpScheme *s)
{
    Cluster clusters[CLUSTERS];
    int base = 0, accent = -1;
    double best_score = -1, bh, bs, bl, ah, as, al;

    if (!path || !cluster_image(path, clusters))
        return FALSE;

    for (int k = 0; k < CLUSTERS; k++) {
        LxpColor c = { clusters[k].r, clusters[k].g, clusters[k].b, 1 };
        double h, sat, l, score;

        if (clusters[k].count > clusters[base].count)
            base = k;
        lxp_color_to_hsl(&c, &h, &sat, &l);
        /* vivid, not too dark or pale, and not a tiny speck */
        score = sat * sqrt(clusters[k].count) * (l > 0.15 && l < 0.85 ? 1 : 0.2);
        if (score > best_score) {
            best_score = score;
            accent = k;
        }
    }

    {
        LxpColor b = { clusters[base].r, clusters[base].g, clusters[base].b, 1 };
        LxpColor a = { clusters[accent].r, clusters[accent].g, clusters[accent].b, 1 };

        lxp_color_to_hsl(&b, &bh, &bs, &bl);
        lxp_color_to_hsl(&a, &ah, &as, &al);
    }
    bs = MIN(bs, 0.25);
    as = MAX(as, 0.45);
    if (dark) {
        s->background = lxp_color_from_hsl(bh, bs, 0.13, 1);
        s->surface = lxp_color_from_hsl(bh, bs, 0.18, 1);
        s->foreground = lxp_color_from_hsl(bh, MIN(bs, 0.12), 0.92, 1);
        s->accent = lxp_color_from_hsl(ah, as, CLAMP(al, 0.55, 0.7), 1);
    } else {
        s->background = lxp_color_from_hsl(bh, MIN(bs, 0.15), 0.95, 1);
        s->surface = lxp_color_from_hsl(bh, MIN(bs, 0.1), 0.99, 1);
        s->foreground = lxp_color_from_hsl(bh, MIN(bs, 0.15), 0.15, 1);
        s->accent = lxp_color_from_hsl(ah, as, CLAMP(al, 0.35, 0.5), 1);
    }
    reset_optional(s);
    return TRUE;
}

/*
 * Inverted lightness, compressed into comfortable ranges: no pure black
 * or white backgrounds, text well readable.
 */
static LxpColor flip_background(const LxpColor *c, gboolean to_dark)
{
    double h, sat, l;

    lxp_color_to_hsl(c, &h, &sat, &l);
    return lxp_color_from_hsl(h, sat, to_dark ? 0.10 + (1 - l) * 0.8 : 1 - l * 0.35, 1);
}

static LxpColor flip_text(const LxpColor *c, gboolean to_dark)
{
    double h, sat, l;

    lxp_color_to_hsl(c, &h, &sat, &l);
    return lxp_color_from_hsl(h, sat, to_dark ? MAX(0.85, 1 - l * 0.6) : MIN(0.25, (1 - l) * 1.9),
                              1);
}

void lxp_palette_flip(LxpScheme *s)
{
    gboolean to_dark = lxp_color_luminance(&s->background) > lxp_color_luminance(&s->foreground);
    double h, sat, l;

    s->background = flip_background(&s->background, to_dark);
    s->surface = flip_background(&s->surface, to_dark);
    s->foreground = flip_text(&s->foreground, to_dark);
    lxp_color_to_hsl(&s->accent, &h, &sat, &l);
    s->accent = lxp_color_from_hsl(h, sat, to_dark ? MAX(l, 0.6) : MIN(l, 0.45), 1);
    reset_optional(s);
}

LxpColor lxp_palette_harmony(const LxpColor *accent, LxpHarmony harmony)
{
    double h, sat, l;
    double turn = harmony == LXP_HARMONY_COMPLEMENTARY ? 180
                : harmony == LXP_HARMONY_TRIADIC ? 120 : 30;

    lxp_color_to_hsl(accent, &h, &sat, &l);
    return lxp_color_from_hsl(h + turn, sat, l, 1);
}
