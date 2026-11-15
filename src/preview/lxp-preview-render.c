/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-preview-render.h"

#include "lxp-error.h"
#include "lxp-icons.h"
#include "lxp-image.h"

#include <math.h>
#include <pango/pangocairo.h>
#include <string.h>

#define VW LXP_PREVIEW_WIDTH
#define VH LXP_PREVIEW_HEIGHT
/* Virtual pixels per point (96 dpi). */
#define PT_TO_PX (96.0 / 72.0)
/* Screen width the virtual canvas stands for, for unscaled wallpapers. */
#define SCREEN_WIDTH 1920.0

typedef struct {
    double x, y, w, h;
} Rect;

typedef struct {
    cairo_t *cr;
    const LxpPreviewModel *m;
    double device_scale;  /* device pixels per virtual pixel */
} Ctx;

/* --- primitives --------------------------------------------------------- */

static void set_color(cairo_t *cr, const LxpColor *c, double alpha)
{
    cairo_set_source_rgba(cr, c->r, c->g, c->b, c->a * alpha);
}

static void fill_rect(cairo_t *cr, Rect r, const LxpColor *c, double alpha)
{
    set_color(cr, c, alpha);
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    cairo_fill(cr);
}

static void rounded(cairo_t *cr, Rect r, double radius)
{
    double rad = MIN(radius, MIN(r.w, r.h) / 2);

    cairo_new_sub_path(cr);
    cairo_arc(cr, r.x + r.w - rad, r.y + rad, rad, -G_PI / 2, 0);
    cairo_arc(cr, r.x + r.w - rad, r.y + r.h - rad, rad, 0, G_PI / 2);
    cairo_arc(cr, r.x + rad, r.y + r.h - rad, rad, G_PI / 2, G_PI);
    cairo_arc(cr, r.x + rad, r.y + rad, rad, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}

static Rect inset(Rect r, double d)
{
    Rect out = { r.x + d, r.y + d, r.w - 2 * d, r.h - 2 * d };
    return out;
}

typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } Align;

static PangoLayout *make_layout(cairo_t *cr, const char *str, const char *family,
                               double size_pt, gboolean bold)
{
    g_autoptr(PangoFontDescription) desc = pango_font_description_new();
    PangoLayout *layout = pango_cairo_create_layout(cr);

    pango_font_description_set_family(desc, family);
    pango_font_description_set_absolute_size(desc, size_pt * PT_TO_PX * PANGO_SCALE);
    pango_font_description_set_weight(desc, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, str, -1);
    return layout;
}

/* Width of <str> in the UI font. */
static double ui_text_width(Ctx *ctx, const char *str)
{
    PangoLayout *layout = make_layout(ctx->cr, str, ctx->m->font_family,
                                      ctx->m->font_size, FALSE);
    int w, h;

    pango_layout_get_pixel_size(layout, &w, &h);
    g_object_unref(layout);
    return w;
}

/* Single line of text, vertically centered in <r>, ellipsized. */
static void text(Ctx *ctx, Rect r, const char *str, const char *family,
                 double size_pt, gboolean bold, const LxpColor *color, Align align)
{
    cairo_t *cr = ctx->cr;
    PangoLayout *layout = make_layout(cr, str, family, size_pt, bold);
    int tw, th;

    pango_layout_set_width(layout, (int)(MAX(r.w, 1) * PANGO_SCALE));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    pango_layout_set_alignment(layout, align == ALIGN_CENTER ? PANGO_ALIGN_CENTER
                                     : align == ALIGN_RIGHT ? PANGO_ALIGN_RIGHT
                                     : PANGO_ALIGN_LEFT);
    pango_layout_get_pixel_size(layout, &tw, &th);

    set_color(cr, color, 1.0);
    cairo_move_to(cr, r.x, r.y + (r.h - th) / 2);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
}

static void ui_text(Ctx *ctx, Rect r, const char *str, const LxpColor *color, Align align)
{
    text(ctx, r, str, ctx->m->font_family, ctx->m->font_size, FALSE, color, align);
}

/* Icon from the theme's icon set; nothing is drawn if none is found. */
static void icon(Ctx *ctx, const char *const *names, double x, double y, double size)
{
    int px = (int)ceil(size * ctx->device_scale);
    const char *path = lxp_icon_lookup(ctx->m->icon_theme, names, MAX((int)size, 16));
    cairo_surface_t *surface = path ? lxp_image_load(path, px, px) : NULL;

    if (!surface)
        return;
    cairo_save(ctx->cr);
    cairo_translate(ctx->cr, x, y);
    cairo_scale(ctx->cr, size / px, size / px);
    cairo_set_source_surface(ctx->cr, surface, 0, 0);
    cairo_paint(ctx->cr);
    cairo_restore(ctx->cr);
}

#define ICON(...) ((const char *const[]){ __VA_ARGS__, NULL })

/* --- desktop ------------------------------------------------------------ */

static void draw_wallpaper(Ctx *ctx)
{
    cairo_t *cr = ctx->cr;
    const LxpPreviewModel *m = ctx->m;
    const char *mode = m->wallpaper_mode;
    int iw, ih;
    double s, dw, dh;
    cairo_surface_t *surface;

    fill_rect(cr, (Rect){ 0, 0, VW, VH }, &m->desktop_bg, 1.0);
    if (!m->wallpaper || g_str_equal(mode, "color") ||
        !lxp_image_get_size(m->wallpaper, &iw, &ih) || iw <= 0 || ih <= 0)
        return;

    if (g_str_equal(mode, "stretch")) {
        dw = VW;
        dh = VH;
    } else if (g_str_equal(mode, "fit")) {
        s = MIN((double)VW / iw, (double)VH / ih);
        dw = iw * s;
        dh = ih * s;
    } else if (g_str_equal(mode, "center") || g_str_equal(mode, "tile")) {
        s = VW / SCREEN_WIDTH;
        dw = iw * s;
        dh = ih * s;
    } else { /* zoom */
        s = MAX((double)VW / iw, (double)VH / ih);
        dw = iw * s;
        dh = ih * s;
    }

    surface = lxp_image_load(m->wallpaper, MAX(1, (int)ceil(dw * ctx->device_scale)),
                             MAX(1, (int)ceil(dh * ctx->device_scale)));
    if (!surface)
        return;

    cairo_save(cr);
    cairo_rectangle(cr, 0, 0, VW, VH);
    cairo_clip(cr);
    if (g_str_equal(mode, "tile")) {
        cairo_pattern_t *pattern = cairo_pattern_create_for_surface(surface);
        cairo_matrix_t matrix;

        cairo_matrix_init_scale(&matrix, ctx->device_scale, ctx->device_scale);
        cairo_pattern_set_matrix(pattern, &matrix);
        cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
        cairo_set_source(cr, pattern);
        cairo_paint(cr);
        cairo_pattern_destroy(pattern);
    } else {
        cairo_translate(cr, (VW - dw) / 2, (VH - dh) / 2);
        cairo_scale(cr, dw / cairo_image_surface_get_width(surface),
                    dh / cairo_image_surface_get_height(surface));
        cairo_set_source_surface(cr, surface, 0, 0);
        cairo_paint(cr);
    }
    cairo_restore(cr);
}

/*
 * What picom's blur shows through a translucent area: the wallpaper,
 * rendered small and scaled back up (stronger blur, smaller copy).
 */
static void draw_blurred_wallpaper(Ctx *ctx, Rect area)
{
    double factor = 4.0 + ctx->m->blur_strength * 2.0;
    int w = MAX(1, (int)(VW / factor)), h = MAX(1, (int)(VH / factor));
    cairo_surface_t *small = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t *scr = cairo_create(small);
    Ctx sctx = { scr, ctx->m, 1.0 / factor };
    cairo_pattern_t *pattern;

    cairo_scale(scr, 1.0 / factor, 1.0 / factor);
    draw_wallpaper(&sctx);
    cairo_destroy(scr);

    cairo_save(ctx->cr);
    cairo_rectangle(ctx->cr, area.x, area.y, area.w, area.h);
    cairo_clip(ctx->cr);
    cairo_scale(ctx->cr, factor, factor);
    cairo_set_source_surface(ctx->cr, small, 0, 0);
    pattern = cairo_get_source(ctx->cr);
    cairo_pattern_set_filter(pattern, CAIRO_FILTER_GOOD);
    cairo_pattern_set_extend(pattern, CAIRO_EXTEND_PAD);
    cairo_paint(ctx->cr);
    cairo_restore(ctx->cr);
    cairo_surface_destroy(small);
}

static void draw_desktop_icons(Ctx *ctx, Rect area)
{
    const struct { const char *label; const char *const *names; } items[] = {
        { "Home",      ICON("user-home", "folder-home", "folder") },
        { "Documents", ICON("folder-documents", "folder") },
        { "Trash",     ICON("user-trash-full", "user-trash") },
    };
    LxpColor white = { 1, 1, 1, 1 }, black = { 0, 0, 0, 1 };

    for (gsize i = 0; i < G_N_ELEMENTS(items); i++) {
        double x = area.x + 20, y = area.y + 20 + i * 100;
        Rect label = { x - 20, y + 52, 96, 22 };
        Rect shadow = { label.x + 1, label.y + 1, label.w, label.h };

        icon(ctx, items[i].names, x + 4, y, 48);
        ui_text(ctx, shadow, items[i].label, &black, ALIGN_CENTER);
        ui_text(ctx, label, items[i].label, &white, ALIGN_CENTER);
    }
}

/* --- Openbox decorations ------------------------------------------------ */

static int button_index(char kind)
{
    switch (kind) {
    case 'C': return LXP_BUTTON_CLOSE;
    case 'M': return LXP_BUTTON_MAX;
    case 'I': return LXP_BUTTON_ICONIFY;
    case 'S': return LXP_BUTTON_SHADE;
    case 'D': return LXP_BUTTON_DESK;
    default:  return -1;
    }
}

/* An Openbox button bitmap, at its own pixel size, centered in <b>. */
static void draw_bitmap(cairo_t *cr, const LxpXbm *xbm, Rect b, const LxpColor *fg)
{
    cairo_surface_t *mask = cairo_image_surface_create(CAIRO_FORMAT_A8, xbm->width, xbm->height);
    int stride = cairo_image_surface_get_stride(mask);
    guint8 *data;

    cairo_surface_flush(mask);
    data = cairo_image_surface_get_data(mask);
    for (int y = 0; y < xbm->height; y++)
        for (int x = 0; x < xbm->width; x++)
            data[y * stride + x] = lxp_xbm_get(xbm, x, y) ? 255 : 0;
    cairo_surface_mark_dirty(mask);

    set_color(cr, fg, 1.0);
    cairo_mask_surface(cr, mask, round(b.x + (b.w - xbm->width) / 2),
                       round(b.y + (b.h - xbm->height) / 2));
    cairo_surface_destroy(mask);
}

static void draw_button_glyph(cairo_t *cr, char kind, Rect b, const LxpColor *fg)
{
    double lw = MAX(1.5, b.w / 9);
    Rect g = inset(b, b.w * 0.28);

    set_color(cr, fg, 1.0);
    cairo_set_line_width(cr, lw);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    switch (kind) {
    case 'I':
        cairo_move_to(cr, g.x, g.y + g.h * 0.85);
        cairo_line_to(cr, g.x + g.w, g.y + g.h * 0.85);
        break;
    case 'M':
        cairo_rectangle(cr, g.x, g.y, g.w, g.h);
        break;
    case 'C':
        cairo_move_to(cr, g.x, g.y);
        cairo_line_to(cr, g.x + g.w, g.y + g.h);
        cairo_move_to(cr, g.x + g.w, g.y);
        cairo_line_to(cr, g.x, g.y + g.h);
        break;
    case 'S':
        cairo_move_to(cr, g.x, g.y + g.h * 0.2);
        cairo_line_to(cr, g.x + g.w, g.y + g.h * 0.2);
        break;
    case 'D':
        cairo_arc(cr, g.x + g.w / 2, g.y + g.h / 2, g.w / 4, 0, 2 * G_PI);
        cairo_fill(cr);
        return;
    default:
        return;
    }
    cairo_stroke(cr);
}

/* The theme's bitmap and color for this button, else a drawn glyph. */
static void draw_title_button(const LxpPreviewModel *m, cairo_t *cr, char kind, Rect b,
                              const LxpDecoration *d)
{
    int i = button_index(kind);
    const LxpColor *fg = i >= 0 ? &d->button_kind_fg[i] : &d->button_fg;

    if (i >= 0 && m->button_bitmaps[i])
        draw_bitmap(cr, m->button_bitmaps[i], b, fg);
    else
        draw_button_glyph(cr, kind, b, fg);
}

static void fill_title(cairo_t *cr, Rect r, const LxpDecoration *d)
{
    if (d->title_gradient) {
        cairo_pattern_t *p = cairo_pattern_create_linear(0, r.y, 0, r.y + r.h);
        cairo_pattern_add_color_stop_rgba(p, 0, d->title_bg.r, d->title_bg.g,
                                          d->title_bg.b, d->title_bg.a);
        cairo_pattern_add_color_stop_rgba(p, 1, d->title_bg_to.r, d->title_bg_to.g,
                                          d->title_bg_to.b, d->title_bg_to.a);
        cairo_set_source(cr, p);
        cairo_rectangle(cr, r.x, r.y, r.w, r.h);
        cairo_fill(cr);
        cairo_pattern_destroy(p);
    } else {
        fill_rect(cr, r, &d->title_bg, 1.0);
    }
}

/* Draw an Openbox frame around <outer>; returns the client area. */
static Rect draw_frame(Ctx *ctx, Rect outer, gboolean active, const char *caption,
                       const char *const *icon_names)
{
    cairo_t *cr = ctx->cr;
    const LxpPreviewModel *m = ctx->m;
    const LxpDecoration *d = active ? &m->active : &m->inactive;
    double bw = m->border_width;
    double font_px = m->title_font_size * PT_TO_PX;
    double th = ceil(font_px * 1.35) + 2 * m->padding_height;
    double bs = MAX(10, th - 2 * m->padding_height);
    Rect title = { outer.x + bw, outer.y + bw, outer.w - 2 * bw, th };
    double left = title.x + m->padding_width;
    double right = title.x + title.w - m->padding_width;
    const char *layout = m->title_layout;
    const char *label_pos = strchr(layout, 'L');
    double handle = m->handle_width > 0 ? m->handle_width + bw : 0;
    Rect client;

    if (bw > 0)
        fill_rect(cr, outer, &d->border, 1.0);
    fill_title(cr, title, d);

    /* Buttons left of the label, from the left edge... */
    for (const char *c = layout; *c && (!label_pos || c < label_pos); c++) {
        Rect b = { left, title.y + (th - bs) / 2, bs, bs };
        left += bs + m->padding_width;
        if (*c == 'N') {
            icon(ctx, icon_names, b.x, b.y, bs);
            continue;
        }
        if (d->button_bg_visible)
            fill_rect(cr, b, &d->button_bg, 1.0);
        draw_title_button(m, cr, *c, b, d);
    }
    /* ...and right of it, from the right edge. */
    if (label_pos) {
        for (const char *c = layout + strlen(layout) - 1; c > label_pos; c--) {
            Rect b = { right - bs, title.y + (th - bs) / 2, bs, bs };
            right -= bs + m->padding_width;
            if (*c == 'N') {
                icon(ctx, icon_names, b.x, b.y, bs);
                continue;
            }
            if (d->button_bg_visible)
                fill_rect(cr, b, &d->button_bg, 1.0);
            draw_title_button(m, cr, *c, b, d);
        }

        {
            Rect label = { left, title.y + m->padding_height, right - left,
                           th - 2 * m->padding_height };
            Align align = g_str_equal(m->title_justify, "center") ? ALIGN_CENTER
                        : g_str_equal(m->title_justify, "right") ? ALIGN_RIGHT
                        : ALIGN_LEFT;

            if (d->label_bg_visible)
                fill_rect(cr, label, &d->label_bg, 1.0);
            text(ctx, label, caption, m->title_font_family,
                 m->title_font_size, m->title_font_bold, &d->label_text, align);
        }
    }

    client.x = outer.x + bw;
    client.y = title.y + th + bw;
    client.w = outer.w - 2 * bw;
    client.h = outer.y + outer.h - bw - handle - client.y;

    if (handle > 0) {
        Rect h = { outer.x + bw, outer.y + outer.h - bw - m->handle_width,
                   outer.w - 2 * bw, m->handle_width };
        LxpColor black = { 0, 0, 0, 1 };
        LxpColor grip = lxp_color_mix(&d->handle, &black, 0.12);
        double gw = MIN(28, h.w / 4);

        fill_rect(cr, h, &d->handle, 1.0);
        fill_rect(cr, (Rect){ h.x, h.y, gw, h.h }, &grip, 1.0);
        fill_rect(cr, (Rect){ h.x + h.w - gw, h.y, gw, h.h }, &grip, 1.0);
    }

    if (m->client_padding > 0) {
        fill_rect(cr, client, &d->client, 1.0);
        client = inset(client, m->client_padding);
    }
    return client;
}

/* --- application windows ------------------------------------------------ */

static void draw_button(Ctx *ctx, Rect r, const char *label)
{
    const LxpPreviewModel *m = ctx->m;
    LxpColor edge = lxp_color_mix(&m->button, &m->window_text, 0.3);

    rounded(ctx->cr, r, ctx->m->widget_radius);
    set_color(ctx->cr, &m->button, 1.0);
    cairo_fill_preserve(ctx->cr);
    set_color(ctx->cr, &edge, 1.0);
    cairo_set_line_width(ctx->cr, 1);
    cairo_stroke(ctx->cr);
    ui_text(ctx, r, label, &m->button_text, ALIGN_CENTER);
}

static void draw_files(Ctx *ctx, Rect c)
{
    const struct { const char *label; const char *const *names; } places[] = {
        { "Home",      ICON("user-home", "folder-home", "folder") },
        { "Desktop",   ICON("user-desktop", "folder") },
        { "Documents", ICON("folder-documents", "folder") },
        { "Downloads", ICON("folder-download", "folder") },
        { "Trash",     ICON("user-trash", "folder") },
    };
    const struct { const char *label; const char *const *names; } files[] = {
        { "Documents", ICON("folder-documents", "folder") },
        { "Music",     ICON("folder-music", "folder") },
        { "Pictures",  ICON("folder-pictures", "folder") },
        { "Projects",  ICON("folder-development", "folder") },
        { "Videos",    ICON("folder-videos", "folder") },
        { "notes.txt", ICON("text-x-generic", "text-plain") },
        { "photo.jpg", ICON("image-x-generic", "image-jpeg") },
    };
    cairo_t *cr = ctx->cr;
    const LxpPreviewModel *m = ctx->m;
    LxpColor line = lxp_color_mix(&m->window, &m->window_text, 0.18);
    Rect toolbar = { c.x, c.y, c.w, 40 };
    Rect status = { c.x, c.y + c.h - 30, c.w, 30 };
    Rect side = { c.x, toolbar.y + toolbar.h, 180, status.y - toolbar.y - toolbar.h };
    Rect view = { side.x + side.w, side.y, c.w - side.w, side.h };
    Rect path = { c.x + 112, c.y + 7, c.w - 124, 26 };

    cairo_save(cr);
    cairo_rectangle(cr, c.x, c.y, c.w, c.h);
    cairo_clip(cr);
    fill_rect(cr, c, &m->window, 1.0);

    /* toolbar */
    icon(ctx, ICON("go-previous", "go-previous-symbolic"), c.x + 10, c.y + 10, 20);
    icon(ctx, ICON("go-next", "go-next-symbolic"), c.x + 44, c.y + 10, 20);
    icon(ctx, ICON("go-up", "go-up-symbolic"), c.x + 78, c.y + 10, 20);
    rounded(cr, path, m->widget_radius);
    set_color(cr, &m->base, 1.0);
    cairo_fill_preserve(cr);
    set_color(cr, &line, 1.0);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    ui_text(ctx, inset(path, 6), "/home/user", &m->text, ALIGN_LEFT);
    fill_rect(cr, (Rect){ c.x, toolbar.y + toolbar.h - 1, c.w, 1 }, &line, 1.0);

    /* sidebar */
    for (gsize i = 0; i < G_N_ELEMENTS(places); i++) {
        Rect row = { side.x + 4, side.y + 8 + i * 30, side.w - 8, 28 };
        const LxpColor *fg = &m->window_text;

        if (i == 0) {
            rounded(cr, row, m->widget_radius);
            set_color(cr, &m->highlight, 1.0);
            cairo_fill(cr);
            fg = &m->highlighted_text;
        }
        icon(ctx, places[i].names, row.x + 6, row.y + 4, 20);
        ui_text(ctx, (Rect){ row.x + 34, row.y, row.w - 38, row.h }, places[i].label,
                fg, ALIGN_LEFT);
    }
    fill_rect(cr, (Rect){ side.x + side.w - 1, side.y, 1, side.h }, &line, 1.0);

    /* file view */
    fill_rect(cr, view, &m->base, 1.0);
    for (gsize i = 0; i < G_N_ELEMENTS(files); i++) {
        int cols = MAX(1, (int)(view.w - 20) / 110);
        Rect cell = { view.x + 10 + (i % cols) * 110, view.y + 12 + (i / cols) * 104, 104, 98 };
        Rect label = { cell.x + 2, cell.y + 64, cell.w - 4, 26 };
        const LxpColor *fg = &m->text;

        if (i == 3) {
            rounded(cr, cell, m->widget_radius + 1);
            set_color(cr, &m->highlight, 0.35);
            cairo_fill(cr);
            rounded(cr, label, m->widget_radius);
            set_color(cr, &m->highlight, 1.0);
            cairo_fill(cr);
            fg = &m->highlighted_text;
        }
        icon(ctx, files[i].names, cell.x + (cell.w - 56) / 2, cell.y + 6, 56);
        ui_text(ctx, label, files[i].label, fg, ALIGN_CENTER);
    }

    /* status bar */
    fill_rect(cr, (Rect){ c.x, status.y, c.w, 1 }, &line, 1.0);
    ui_text(ctx, (Rect){ status.x + 10, status.y, 160, status.h }, "7 items",
            &m->window_text, ALIGN_LEFT);
    ui_text(ctx, (Rect){ status.x + 170, status.y, 220, status.h }, "Open terminal here",
            &m->link, ALIGN_LEFT);
    draw_button(ctx, (Rect){ status.x + status.w - 96, status.y + 3, 86, status.h - 6 }, "Open");
    cairo_restore(cr);
}

static void draw_editor(Ctx *ctx, Rect c)
{
    static const char *const lines[] = {
        "# lxqt-personalizer",
        "",
        "Create, preview and apply themes",
        "for LXQt and Openbox.",
        "",
        "- palette, icons and fonts",
        "- window decorations",
        "- wallpaper and panel",
    };
    cairo_t *cr = ctx->cr;
    const LxpPreviewModel *m = ctx->m;
    LxpColor line = lxp_color_mix(&m->window, &m->window_text, 0.18);
    static const char *const menus[] = { "File", "Edit", "View", "Help" };
    Rect text_area = { c.x, c.y + 28, c.w, c.h - 28 };
    Rect gutter = { text_area.x, text_area.y, 40, text_area.h };

    cairo_save(cr);
    cairo_rectangle(cr, c.x, c.y, c.w, c.h);
    cairo_clip(cr);
    fill_rect(cr, c, &m->window, 1.0);
    for (gsize i = 0; i < G_N_ELEMENTS(menus); i++)
        ui_text(ctx, (Rect){ c.x + 10 + i * 56, c.y, 52, 28 }, menus[i],
                &m->window_text, ALIGN_LEFT);
    fill_rect(cr, (Rect){ c.x, c.y + 27, c.w, 1 }, &line, 1.0);

    fill_rect(cr, text_area, &m->base, 1.0);
    fill_rect(cr, gutter, &m->alt_base, 1.0);
    for (gsize i = 0; i < G_N_ELEMENTS(lines); i++) {
        Rect row = { gutter.x + gutter.w + 8, text_area.y + 6 + i * 24, c.w - 60, 24 };
        g_autofree char *num = g_strdup_printf("%zu", i + 1);
        LxpColor dim = lxp_color_mix(&m->text, &m->base, 0.5);

        ui_text(ctx, (Rect){ gutter.x, row.y, gutter.w - 8, row.h }, num, &dim, ALIGN_RIGHT);
        if (i == 2) {
            /* "Create, preview and" selected: highlighted text, clipped */
            Rect sel = { row.x - 1, row.y + 2,
                         ui_text_width(ctx, "Create, preview and") + 2, row.h - 4 };

            fill_rect(cr, sel, &m->highlight, 1.0);
            ui_text(ctx, row, lines[i], &m->text, ALIGN_LEFT);
            cairo_save(cr);
            cairo_rectangle(cr, sel.x, sel.y, sel.w, sel.h);
            cairo_clip(cr);
            fill_rect(cr, sel, &m->highlight, 1.0);
            ui_text(ctx, row, lines[i], &m->highlighted_text, ALIGN_LEFT);
            cairo_restore(cr);
        } else {
            ui_text(ctx, row, lines[i], &m->text, ALIGN_LEFT);
        }
    }
    cairo_restore(cr);
}

static void draw_openbox_menu(Ctx *ctx, double x, double y)
{
    static const char *const items[] = {
        "Terminal", "Web browser", "File manager", NULL, "Reconfigure", "Log out",
    };
    cairo_t *cr = ctx->cr;
    const LxpPreviewModel *m = ctx->m;
    double bw = m->menu_border_width, w = 220, item_h = 26, title_h = 28;
    double h = title_h + bw;
    double cy;

    for (gsize i = 0; i < G_N_ELEMENTS(items); i++)
        h += items[i] ? item_h : 9;
    h += 2 * bw + 4;

    fill_rect(cr, (Rect){ x, y, w, h }, &m->menu_border, 1.0);
    fill_rect(cr, (Rect){ x + bw, y + bw, w - 2 * bw, title_h }, &m->menu_title_bg, 1.0);
    text(ctx, (Rect){ x + bw, y + bw, w - 2 * bw, title_h }, "Openbox",
         m->title_font_family, m->title_font_size, TRUE, &m->menu_title_text, ALIGN_CENTER);

    cy = y + bw + title_h + bw;
    fill_rect(cr, (Rect){ x + bw, cy, w - 2 * bw, h - (cy - y) - bw }, &m->menu_bg, 1.0);
    cy += 2;
    for (gsize i = 0; i < G_N_ELEMENTS(items); i++) {
        Rect row = { x + bw, cy, w - 2 * bw, items[i] ? item_h : 9 };

        if (!items[i]) {
            fill_rect(cr, (Rect){ row.x + 8, row.y + 4, row.w - 16, 1 }, &m->menu_text, 0.3);
        } else {
            gboolean hover = i == 1;
            if (hover)
                fill_rect(cr, row, &m->menu_active_bg, 1.0);
            ui_text(ctx, (Rect){ row.x + 14, row.y, row.w - 20, row.h }, items[i],
                    hover ? &m->menu_active_text : &m->menu_text, ALIGN_LEFT);
        }
        cy += row.h;
    }
}

/* --- panel -------------------------------------------------------------- */

/* Length of the panel along its edge, and where it starts on that edge. */
static void panel_extent(const LxpPreviewModel *m, double side, double *start, double *len)
{
    double l = m->panel_width_percent ? side * m->panel_width / 100.0
                                      : m->panel_width * VW / SCREEN_WIDTH;

    l = CLAMP(l, 1, side);
    *len = l;
    *start = m->panel_alignment < 0 ? 0 : m->panel_alignment > 0 ? side - l : (side - l) / 2;
}

static Rect panel_rect(const LxpPreviewModel *m)
{
    double s = m->panel_size;
    const char *pos = m->panel_position;
    double start, len;

    if (g_str_equal(pos, "Left") || g_str_equal(pos, "Right")) {
        panel_extent(m, VH, &start, &len);
        return (Rect){ g_str_equal(pos, "Left") ? 0 : VW - s, start, s, len };
    }
    panel_extent(m, VW, &start, &len);
    return (Rect){ start, g_str_equal(pos, "Top") ? 0 : VH - s, len, s };
}

static Rect work_area(const LxpPreviewModel *m)
{
    Rect p = panel_rect(m);
    const char *pos = m->panel_position;

    if (!m->panel_reserve_space)
        return (Rect){ 0, 0, VW, VH };
    if (g_str_equal(pos, "Top"))
        return (Rect){ 0, p.h, VW, VH - p.h };
    if (g_str_equal(pos, "Left"))
        return (Rect){ p.w, 0, VW - p.w, VH };
    if (g_str_equal(pos, "Right"))
        return (Rect){ 0, 0, VW - p.w, VH };
    return (Rect){ 0, 0, VW, VH - p.h };
}

static void draw_panel(Ctx *ctx)
{
    const char *const *const launchers[] = {
        ICON("utilities-terminal", "terminal", "qterminal"),
        ICON("system-file-manager", "folder"),
        ICON("internet-web-browser", "web-browser", "firefox"),
    };
    cairo_t *cr = ctx->cr;
    const LxpPreviewModel *m = ctx->m;
    Rect p = panel_rect(m);
    gboolean horizontal = p.w > p.h;
    double thick = horizontal ? p.h : p.w;
    double isz = MIN(m->panel_icon_size, thick - 6);
    double off = (thick - isz) / 2;
    double pos = 6;
    LxpColor edge = lxp_color_mix(&m->panel_bg, &m->panel_text, 0.2);

    cairo_save(cr);
    cairo_rectangle(cr, p.x, p.y, p.w, p.h);
    cairo_clip(cr);
    if (m->blur && m->panel_opacity < 1)
        draw_blurred_wallpaper(ctx, p);
    if (m->panel_gradient) {
        /* themes shade the panel across its thickness */
        cairo_pattern_t *shade = horizontal
            ? cairo_pattern_create_linear(0, p.y, 0, p.y + p.h)
            : cairo_pattern_create_linear(p.x, 0, p.x + p.w, 0);

        cairo_pattern_add_color_stop_rgba(shade, 0, m->panel_bg.r, m->panel_bg.g,
                                          m->panel_bg.b, m->panel_opacity);
        cairo_pattern_add_color_stop_rgba(shade, 1, m->panel_bg_to.r, m->panel_bg_to.g,
                                          m->panel_bg_to.b, m->panel_opacity);
        cairo_set_source(cr, shade);
        cairo_rectangle(cr, p.x, p.y, p.w, p.h);
        cairo_fill(cr);
        cairo_pattern_destroy(shade);
    } else {
        fill_rect(cr, p, &m->panel_bg, m->panel_opacity);
    }
    if (g_strcmp0(m->panel_style, "theme") == 0)
        ;  /* an installed theme draws its own edge, or none */
    else if (horizontal)
        fill_rect(cr, (Rect){ p.x, g_str_equal(m->panel_position, "Top") ? p.y + p.h - 1 : p.y,
                              p.w, 1 }, &edge, m->panel_opacity);
    else
        fill_rect(cr, (Rect){ g_str_equal(m->panel_position, "Left") ? p.x + p.w - 1 : p.x,
                              p.y, 1, p.h }, &edge, m->panel_opacity);

#define AT(along, size) (horizontal ? p.x + (along) : p.x + off), \
                        (horizontal ? p.y + off : p.y + (along))

    icon(ctx, ICON("start-here", "application-menu", "view-app-grid", "open-menu"),
         AT(pos, isz), isz);
    pos += isz + 14;
    for (gsize i = 0; i < G_N_ELEMENTS(launchers); i++) {
        icon(ctx, launchers[i], AT(pos, isz), isz);
        pos += isz + 8;
    }
    pos += 10;

    /*
     * Task buttons, the active "Files" window first, sharing the room left
     * between the launchers and the tray; icon-only when it gets tight.
     */
    double room = (horizontal ? p.w : p.h) - pos - (horizontal ? 86 + 2 * isz : 64 + isz);
    double task_len = horizontal ? CLAMP(room / 2 - 4, isz + 12, 200) : thick - 4;
    gboolean labels = horizontal && task_len >= isz + 60;

    for (int i = 0; i < 2 && room >= 2 * (isz + 16); i++) {
        const char *label = i == 0 ? "Files — Home" : "Text Editor";
        const char *const *names = i == 0 ? ICON("system-file-manager", "folder")
                                          : ICON("accessories-text-editor", "text-editor");
        double len = task_len;
        Rect b = horizontal ? (Rect){ p.x + pos, p.y + 3, len, p.h - 6 }
                            : (Rect){ p.x + 2, p.y + pos, thick - 4, len };

        const LxpColor *task_text = &m->panel_text;

        if (g_strcmp0(m->panel_style, "theme") == 0) {
            /* as the installed LXQt theme styles them */
            const LxpPanelTaskStyle *st = i == 0 ? &m->panel_task_active : &m->panel_task;
            Rect in = inset(b, 1);

            if (st->has_bg) {
                rounded(cr, in, 3);
                set_color(cr, &st->bg, 1);
                cairo_fill(cr);
            } else if (i == 0 && !st->has_edge) {
                /* nothing set: Qt's sunken look for the pressed button */
                rounded(cr, in, 3);
                set_color(cr, &m->panel_text, 0.14);
                cairo_fill(cr);
            }
            if (st->has_edge) {
                double w = 2;

                switch (st->edge_side) {
                case 't': fill_rect(cr, (Rect){ in.x, in.y, in.w, w }, &st->edge, 1); break;
                case 'b': fill_rect(cr, (Rect){ in.x, in.y + in.h - w, in.w, w }, &st->edge, 1); break;
                case 'l': fill_rect(cr, (Rect){ in.x, in.y, w, in.h }, &st->edge, 1); break;
                case 'r': fill_rect(cr, (Rect){ in.x + in.w - w, in.y, w, in.h }, &st->edge, 1); break;
                default:
                    rounded(cr, inset(in, 0.5), 3);
                    set_color(cr, &st->edge, 1);
                    cairo_set_line_width(cr, 1);
                    cairo_stroke(cr);
                }
            }
            if (st->has_text)
                task_text = &st->text;
        } else if (g_strcmp0(m->panel_style, "pills") == 0) {
            Rect pill = inset(b, 1);
            rounded(cr, pill, MIN(pill.w, pill.h) / 2);
            if (i == 0)
                set_color(cr, &m->highlight, 0.55);
            else
                set_color(cr, &m->panel_text, 0.06);
            cairo_fill(cr);
        } else if (g_strcmp0(m->panel_style, "flat") == 0) {
            cairo_rectangle(cr, b.x, p.y, b.w, horizontal ? p.h : b.h);
            set_color(cr, &m->panel_text, i == 0 ? 0.16 : 0);
            cairo_fill(cr);
        } else {
            rounded(cr, b, 3);
            if (i == 0)
                set_color(cr, &m->highlight, 0.30);
            else
                set_color(cr, &m->panel_text, 0.08);
            cairo_fill(cr);
            if (i == 0 && horizontal)
                fill_rect(cr, (Rect){ b.x, b.y + b.h - 2, b.w, 2 }, &m->highlight, 1);
        }
        if (labels) {
            icon(ctx, names, b.x + 6, p.y + off, isz);
            ui_text(ctx, (Rect){ b.x + isz + 12, b.y, b.w - isz - 18, b.h }, label,
                    task_text, ALIGN_LEFT);
        } else if (horizontal) {
            icon(ctx, names, b.x + (b.w - isz) / 2, p.y + off, isz);
        } else {
            icon(ctx, names, p.x + off, b.y + (b.h - isz) / 2, isz);
        }
        pos += len + 4;
    }

    /* tray and clock at the far end */
    if (horizontal) {
        double end = p.x + p.w - 8;
        ui_text(ctx, (Rect){ end - 60, p.y, 60, p.h }, "14:32", &m->panel_text, ALIGN_RIGHT);
        end -= 70 + isz;
        icon(ctx, ICON("audio-volume-high", "audio-volume-high-symbolic"), end, p.y + off, isz);
        end -= isz + 8;
        icon(ctx, ICON("network-wireless", "network-wired", "network-transmit-receive"),
             end, p.y + off, isz);
    } else {
        double end = p.y + p.h - 8;
        ui_text(ctx, (Rect){ p.x, end - 40, p.w, 20 }, "14", &m->panel_text, ALIGN_CENTER);
        ui_text(ctx, (Rect){ p.x, end - 20, p.w, 20 }, "32", &m->panel_text, ALIGN_CENTER);
        icon(ctx, ICON("audio-volume-high", "audio-volume-high-symbolic"),
             p.x + off, end - 52 - isz, isz);
    }
#undef AT
    cairo_restore(cr);
}

/* --- compositor ---------------------------------------------------------- */

/*
 * picom's shadow: the window shape, blurred by shadow_radius and moved by
 * the offsets (which already include the blur margin, so -radius centers
 * it). Approximated with stacked translucent rounded rectangles.
 */
static void draw_shadow(Ctx *ctx, Rect w)
{
    const LxpPreviewModel *m = ctx->m;
    int steps = MAX(1, m->shadow_radius);
    double r = m->shadow_radius;
    double alpha = 1 - pow(1 - m->shadow_opacity, 1.0 / steps);
    Rect core = { w.x + m->shadow_offset_x + r, w.y + m->shadow_offset_y + r, w.w, w.h };

    if (!m->shadow || m->shadow_opacity <= 0)
        return;
    cairo_set_source_rgba(ctx->cr, 0, 0, 0, alpha);
    for (int k = steps; k >= 1; k--) {
        double grow = r * k / steps - r / 2;
        rounded(ctx->cr, (Rect){ core.x - grow, core.y - grow, core.w + 2 * grow,
                                 core.h + 2 * grow }, m->corner_radius + MAX(0, grow));
        cairo_fill(ctx->cr);
    }
}

/* Clip what follows to the window's rounded shape; pair with cairo_restore. */
static void clip_window(Ctx *ctx, Rect w)
{
    cairo_save(ctx->cr);
    if (ctx->m->corner_radius > 0) {
        rounded(ctx->cr, w, ctx->m->corner_radius);
        cairo_clip(ctx->cr);
    }
}

/* --- scene -------------------------------------------------------------- */

static double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void lxp_preview_render(cairo_t *cr, const LxpPreviewModel *m, double width, double height)
{
    double scale = MIN(width / VW, height / VH);
    double dx = 1, dy = 0;
    Ctx ctx = { cr, m, 1 };
    Rect wa;
    Rect editor, files;
    Rect client;

    cairo_save(cr);
    cairo_translate(cr, (width - VW * scale) / 2, (height - VH * scale) / 2);
    cairo_scale(cr, scale, scale);
    cairo_user_to_device_distance(cr, &dx, &dy);
    ctx.device_scale = MAX(0.05, hypot(dx, dy));

    cairo_rectangle(cr, 0, 0, VW, VH);
    cairo_clip(cr);

    wa = work_area(m);
    draw_wallpaper(&ctx);
    draw_desktop_icons(&ctx, wa);

    editor = (Rect){ wa.x + wa.w * 0.40, wa.y + 40, 700, 420 };
    editor.x = clampd(editor.x, wa.x, wa.x + wa.w - editor.w);
    /* the inactive window, with picom's inactive-opacity */
    cairo_push_group(cr);
    draw_shadow(&ctx, editor);
    clip_window(&ctx, editor);
    client = draw_frame(&ctx, editor, FALSE, "notes.md — Text Editor",
                        ICON("accessories-text-editor", "text-editor"));
    draw_editor(&ctx, client);
    cairo_restore(cr);
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, m->inactive_opacity);

    files = (Rect){ wa.x + 150, wa.y + wa.h - 540, 820, 500 };
    files.y = clampd(files.y, wa.y + 10, wa.y + wa.h - files.h);
    draw_shadow(&ctx, files);
    clip_window(&ctx, files);
    client = draw_frame(&ctx, files, TRUE, "Files — Home",
                        ICON("system-file-manager", "folder"));
    draw_files(&ctx, client);
    cairo_restore(cr);

    draw_openbox_menu(&ctx, wa.x + wa.w - 250, wa.y + wa.h - 300);
    draw_panel(&ctx);
    cairo_restore(cr);
}

gboolean lxp_preview_render_png(const LxpPreviewModel *model, int width, int height,
                                const char *path, GError **error)
{
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);
    cairo_status_t status;

    lxp_preview_render(cr, model, width, height);
    cairo_destroy(cr);
    status = cairo_surface_write_to_png(surface, path);
    cairo_surface_destroy(surface);

    if (status != CAIRO_STATUS_SUCCESS) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_FAILED, "Failed to write %s: %s",
                    path, cairo_status_to_string(status));
        return FALSE;
    }
    return TRUE;
}
