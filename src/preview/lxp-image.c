/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-image.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <librsvg/rsvg.h>
#include <math.h>

#define CACHE_MAX 64

static GHashTable *cache;  /* "path\nw\nh" -> surface (or NULL on failure) */

static gboolean is_svg(const char *path)
{
    return g_str_has_suffix(path, ".svg") || g_str_has_suffix(path, ".svgz");
}

gboolean lxp_image_get_size(const char *path, int *width, int *height)
{
    if (path && is_svg(path)) {
        RsvgHandle *handle = rsvg_handle_new_from_file(path, NULL);
        double w, h;
        gboolean ok = handle && rsvg_handle_get_intrinsic_size_in_pixels(handle, &w, &h);

        g_clear_object(&handle);
        if (ok) {
            *width = (int)ceil(w);
            *height = (int)ceil(h);
        }
        return ok;
    }
    return path && gdk_pixbuf_get_file_info(path, width, height) != NULL;
}

/* SVG through librsvg: gdk-pixbuf may not have an SVG loader. */
static cairo_surface_t *load_svg(const char *path, int width, int height)
{
    RsvgHandle *handle = rsvg_handle_new_from_file(path, NULL);
    RsvgRectangle viewport = { 0, 0, width, height };
    cairo_surface_t *surface;
    cairo_t *cr;
    gboolean ok;

    if (!handle)
        return NULL;
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cr = cairo_create(surface);
    ok = rsvg_handle_render_document(handle, cr, &viewport, NULL);
    cairo_destroy(cr);
    g_object_unref(handle);
    if (!ok) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    return surface;
}

/* GdkPixbuf (RGBA, not premultiplied) -> cairo ARGB32 (premultiplied). */
static cairo_surface_t *surface_from_pixbuf(GdkPixbuf *pixbuf)
{
    int w = gdk_pixbuf_get_width(pixbuf);
    int h = gdk_pixbuf_get_height(pixbuf);
    int channels = gdk_pixbuf_get_n_channels(pixbuf);
    int src_stride = gdk_pixbuf_get_rowstride(pixbuf);
    const guint8 *src = gdk_pixbuf_read_pixels(pixbuf);
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    int dst_stride = cairo_image_surface_get_stride(surface);
    guint8 *dst;

    cairo_surface_flush(surface);
    dst = cairo_image_surface_get_data(surface);
    for (int y = 0; y < h; y++) {
        const guint8 *s = src + y * src_stride;
        guint32 *d = (guint32 *)(dst + y * dst_stride);

        for (int x = 0; x < w; x++, s += channels) {
            guint a = channels == 4 ? s[3] : 255;
            guint r = (s[0] * a + 127) / 255;
            guint g = (s[1] * a + 127) / 255;
            guint b = (s[2] * a + 127) / 255;
            d[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    cairo_surface_mark_dirty(surface);
    return surface;
}

cairo_surface_t *lxp_image_load(const char *path, int width, int height)
{
    g_autofree char *key = NULL;
    g_autoptr(GdkPixbuf) pixbuf = NULL;
    cairo_surface_t *surface;

    if (!path || width <= 0 || height <= 0)
        return NULL;
    if (!cache)
        cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                      (GDestroyNotify)cairo_surface_destroy);

    key = g_strdup_printf("%s\n%d\n%d", path, width, height);
    if (g_hash_table_lookup_extended(cache, key, NULL, (gpointer *)&surface))
        return surface;

    /* Crude bound: thumbnails at many sizes would otherwise pile up. */
    if (g_hash_table_size(cache) >= CACHE_MAX)
        g_hash_table_remove_all(cache);

    if (is_svg(path)) {
        surface = load_svg(path, width, height);
    } else {
        pixbuf = gdk_pixbuf_new_from_file_at_scale(path, width, height, FALSE, NULL);
        surface = pixbuf ? surface_from_pixbuf(pixbuf) : NULL;
    }
    g_hash_table_insert(cache, g_steal_pointer(&key), surface);
    return surface;
}

void lxp_image_cache_clear(void)
{
    if (cache)
        g_hash_table_remove_all(cache);
}
