/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-wallgen.h"

#include "lxp-error.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <math.h>

const char *const lxp_wallgen_styles[] = { "gradient", "blobs", "waves", "geometric", NULL };

gboolean lxp_wallgen_is_style(const char *style)
{
    return style && g_strv_contains(lxp_wallgen_styles, style);
}

static void source(cairo_t *cr, const LxpColor *c, double alpha)
{
    cairo_set_source_rgba(cr, c->r, c->g, c->b, alpha);
}

static void stop(cairo_pattern_t *p, double offset, const LxpColor *c, double alpha)
{
    cairo_pattern_add_color_stop_rgba(p, offset, c->r, c->g, c->b, alpha);
}

/* The accent with its hue turned by <degrees> and lightness moved by <dl>. */
static LxpColor accent_variant(const LxpSchemeColors *c, double degrees, double dl)
{
    double h, s, l;

    lxp_color_to_hsl(&c->accent, &h, &s, &l);
    return lxp_color_from_hsl(h + degrees, MIN(1, s * 0.95),
                              CLAMP(l + dl, c->dark ? 0.18 : 0.35, c->dark ? 0.7 : 0.85), 1);
}

/* Background tones: a little deeper than the window background. */
static LxpColor deep_bg(const LxpSchemeColors *c, double amount)
{
    LxpColor edge = { c->dark ? 0 : 1, c->dark ? 0 : 1, c->dark ? 0 : 1, 1 };
    return lxp_color_mix(&c->bg, &edge, c->dark ? amount : amount * 0.3);
}

static void draw_gradient(cairo_t *cr, const LxpSchemeColors *c, GRand *rand, int w, int h)
{
    LxpColor a = deep_bg(c, 0.35);
    LxpColor tint = accent_variant(c, g_rand_double_range(rand, -20, 20), 0);
    LxpColor mid = lxp_color_mix(&c->bg, &tint, 0.35);
    cairo_pattern_t *p = cairo_pattern_create_linear(0, 0, w, h);
    double gx = g_rand_double_range(rand, 0.6, 0.95) * w;
    double gy = g_rand_double_range(rand, 0.05, 0.4) * h;

    stop(p, 0, &a, 1);
    stop(p, 0.55, &mid, 1);
    stop(p, 1, &c->bg, 1);
    cairo_set_source(cr, p);
    cairo_paint(cr);
    cairo_pattern_destroy(p);

    /* soft glow of the accent */
    p = cairo_pattern_create_radial(gx, gy, 0, gx, gy, MAX(w, h) * 0.6);
    stop(p, 0, &tint, 0.45);
    stop(p, 1, &tint, 0);
    cairo_set_source(cr, p);
    cairo_paint(cr);
    cairo_pattern_destroy(p);
}

static void draw_blobs(cairo_t *cr, const LxpSchemeColors *c, GRand *rand, int w, int h)
{
    int n = g_rand_int_range(rand, 5, 8);
    double size = MAX(w, h);
    LxpColor base = deep_bg(c, 0.25);

    source(cr, &base, 1);
    cairo_paint(cr);
    for (int i = 0; i < n; i++) {
        double x = g_rand_double_range(rand, -0.1, 1.1) * w;
        double y = g_rand_double_range(rand, -0.1, 1.1) * h;
        double r = g_rand_double_range(rand, 0.25, 0.6) * size;
        LxpColor col = accent_variant(c, g_rand_double_range(rand, -70, 70),
                                      g_rand_double_range(rand, -0.15, 0.1));
        cairo_pattern_t *p = cairo_pattern_create_radial(x, y, 0, x, y, r);

        stop(p, 0, &col, c->dark ? 0.75 : 0.6);
        stop(p, 0.6, &col, c->dark ? 0.25 : 0.2);
        stop(p, 1, &col, 0);
        cairo_set_source(cr, p);
        cairo_paint(cr);
        cairo_pattern_destroy(p);
    }
}

static void draw_waves(cairo_t *cr, const LxpSchemeColors *c, GRand *rand, int w, int h)
{
    int bands = 5;
    LxpColor top = deep_bg(c, 0.4);
    cairo_pattern_t *sky = cairo_pattern_create_linear(0, 0, 0, h);
    double phase = g_rand_double_range(rand, 0, 2 * G_PI);
    double hue = g_rand_double_range(rand, -25, 25);

    stop(sky, 0, &top, 1);
    stop(sky, 1, &c->bg, 1);
    cairo_set_source(cr, sky);
    cairo_paint(cr);
    cairo_pattern_destroy(sky);

    for (int b = 0; b < bands; b++) {
        double t = (double)b / (bands - 1);
        double base = h * (0.42 + 0.12 * b);
        double amp = h * g_rand_double_range(rand, 0.04, 0.09);
        double freq = g_rand_double_range(rand, 1.2, 2.4) * 2 * G_PI / w;
        LxpColor col = accent_variant(c, hue + 25 * t, -0.18 + 0.22 * t);
        LxpColor fade = lxp_color_mix(&col, &c->bg, 0.35);
        cairo_pattern_t *p = cairo_pattern_create_linear(0, base - amp, 0, h);

        cairo_move_to(cr, 0, h);
        for (int x = 0; x <= w; x += 4) {
            double y = base + amp * sin(freq * x + phase + b * 0.9)
                     + amp * 0.4 * sin(freq * 2.3 * x + phase * 1.7);
            cairo_line_to(cr, x, y);
        }
        cairo_line_to(cr, w, h);
        cairo_close_path(cr);
        stop(p, 0, &col, 0.55 + 0.1 * t);
        stop(p, 1, &fade, 0.9);
        cairo_set_source(cr, p);
        cairo_fill(cr);
        cairo_pattern_destroy(p);
    }
}

static void draw_geometric(cairo_t *cr, const LxpSchemeColors *c, GRand *rand, int w, int h)
{
    int cols = 14, rows = 9;
    double cw = (double)w / cols, ch = (double)h / rows;
    double (*px)[cols + 1] = g_malloc(sizeof(double[rows + 1][cols + 1]));
    double (*py)[cols + 1] = g_malloc(sizeof(double[rows + 1][cols + 1]));
    LxpColor from = deep_bg(c, 0.35);
    LxpColor to = lxp_color_mix(&c->bg, &c->accent, c->dark ? 0.45 : 0.35);

    for (int r = 0; r <= rows; r++) {
        for (int k = 0; k <= cols; k++) {
            gboolean edge = r == 0 || r == rows || k == 0 || k == cols;
            px[r][k] = k * cw + (edge ? 0 : g_rand_double_range(rand, -0.35, 0.35) * cw);
            py[r][k] = r * ch + (edge ? 0 : g_rand_double_range(rand, -0.35, 0.35) * ch);
        }
    }

    for (int r = 0; r < rows; r++) {
        for (int k = 0; k < cols; k++) {
            /* two triangles per cell, shaded along the diagonal */
            for (int tri = 0; tri < 2; tri++) {
                double xs[3], ys[3], cx = 0, cy = 0, t, jitter;
                LxpColor col;

                if (tri == 0) {
                    xs[0] = px[r][k];         ys[0] = py[r][k];
                    xs[1] = px[r][k + 1];     ys[1] = py[r][k + 1];
                    xs[2] = px[r + 1][k];     ys[2] = py[r + 1][k];
                } else {
                    xs[0] = px[r][k + 1];     ys[0] = py[r][k + 1];
                    xs[1] = px[r + 1][k + 1]; ys[1] = py[r + 1][k + 1];
                    xs[2] = px[r + 1][k];     ys[2] = py[r + 1][k];
                }
                for (int i = 0; i < 3; i++) {
                    cx += xs[i] / 3;
                    cy += ys[i] / 3;
                }
                t = CLAMP((cx / w + cy / h) / 2, 0, 1);
                col = lxp_color_mix(&from, &to, t);
                jitter = g_rand_double_range(rand, -0.035, 0.035);
                col.r = CLAMP(col.r + jitter, 0, 1);
                col.g = CLAMP(col.g + jitter, 0, 1);
                col.b = CLAMP(col.b + jitter, 0, 1);

                cairo_move_to(cr, xs[0], ys[0]);
                cairo_line_to(cr, xs[1], ys[1]);
                cairo_line_to(cr, xs[2], ys[2]);
                cairo_close_path(cr);
                source(cr, &col, 1);
                cairo_fill_preserve(cr);
                cairo_set_line_width(cr, 1);
                cairo_stroke(cr);  /* same color: hides hairline seams */
            }
        }
    }
    g_free(px);
    g_free(py);
}

void lxp_wallgen_draw(cairo_t *cr, const LxpSchemeColors *c, const char *style,
                      guint32 seed, int w, int h)
{
    g_autoptr(GRand) rand = g_rand_new_with_seed(seed);

    cairo_save(cr);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_clip(cr);
    if (g_strcmp0(style, "blobs") == 0)
        draw_blobs(cr, c, rand, w, h);
    else if (g_strcmp0(style, "waves") == 0)
        draw_waves(cr, c, rand, w, h);
    else if (g_strcmp0(style, "geometric") == 0)
        draw_geometric(cr, c, rand, w, h);
    else
        draw_gradient(cr, c, rand, w, h);
    cairo_restore(cr);
}

gboolean lxp_wallgen_render_png(const LxpSchemeColors *c, const char *style, guint32 seed,
                                int width, int height, const char *path, GError **error)
{
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    cairo_t *cr = cairo_create(surface);
    g_autofree char *tmp = g_strconcat(path, ".tmp", NULL);
    cairo_status_t status;

    lxp_wallgen_draw(cr, c, style, seed, width, height);
    cairo_destroy(cr);
    status = cairo_surface_write_to_png(surface, tmp);
    cairo_surface_destroy(surface);
    if (status != CAIRO_STATUS_SUCCESS || g_rename(tmp, path) != 0) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_FAILED, "Failed to write %s: %s", path,
                    status != CAIRO_STATUS_SUCCESS ? cairo_status_to_string(status)
                                                   : g_strerror(errno));
        g_unlink(tmp);
        return FALSE;
    }
    return TRUE;
}
