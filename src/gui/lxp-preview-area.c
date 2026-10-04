/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-preview-area.h"

#include "lxp-preview-render.h"

typedef struct {
    LxpPreviewModel *model;
    cairo_surface_t *cache;   /* rendering at cache_w x cache_h device px */
    int cache_w, cache_h;
} AreaData;

static void area_data_free(AreaData *d)
{
    lxp_preview_model_free(d->model);
    g_clear_pointer(&d->cache, cairo_surface_destroy);
    g_free(d);
}

static AreaData *area_data(GtkWidget *area)
{
    return g_object_get_data(G_OBJECT(area), "lxp-preview");
}

static void draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user)
{
    AreaData *d = area_data(GTK_WIDGET(area));
    int scale = gtk_widget_get_scale_factor(GTK_WIDGET(area));
    int pw = width * scale, ph = height * scale;

    if (!d->model)
        return;

    if (!d->cache || d->cache_w != pw || d->cache_h != ph) {
        cairo_t *c;

        g_clear_pointer(&d->cache, cairo_surface_destroy);
        d->cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
        cairo_surface_set_device_scale(d->cache, scale, scale);
        c = cairo_create(d->cache);
        lxp_preview_render(c, d->model, width, height);
        cairo_destroy(c);
        d->cache_w = pw;
        d->cache_h = ph;
    }
    cairo_set_source_surface(cr, d->cache, 0, 0);
    cairo_paint(cr);
}

GtkWidget *lxp_preview_area_new(int width, int height)
{
    GtkWidget *area = gtk_drawing_area_new();

    g_object_set_data_full(G_OBJECT(area), "lxp-preview", g_new0(AreaData, 1),
                           (GDestroyNotify)area_data_free);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), width);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), height);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw, NULL, NULL);
    return area;
}

void lxp_preview_area_set_model(GtkWidget *area, LxpPreviewModel *model)
{
    AreaData *d = area_data(area);

    lxp_preview_model_free(d->model);
    d->model = model;
    g_clear_pointer(&d->cache, cairo_surface_destroy);
    gtk_widget_queue_draw(area);
}
