/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <glib.h>

/*
 * X bitmaps (.xbm), as used by Openbox for title bar buttons: rows of
 * bits, least significant bit first, each row padded to whole bytes.
 */
typedef struct {
    int     width, height;
    guint8 *bits;
} LxpXbm;

LxpXbm  *lxp_xbm_new(int width, int height);
LxpXbm  *lxp_xbm_parse(const char *text);
LxpXbm  *lxp_xbm_load(const char *path);
void     lxp_xbm_free(LxpXbm *xbm);

gboolean lxp_xbm_get(const LxpXbm *xbm, int x, int y);
void     lxp_xbm_set(LxpXbm *xbm, int x, int y);
char    *lxp_xbm_to_text(const LxpXbm *xbm, const char *name);

/* Drawing helpers for generated button sets (coordinates in pixels). */
void     lxp_xbm_fill_circle(LxpXbm *xbm, double cx, double cy, double r);
void     lxp_xbm_line(LxpXbm *xbm, double x0, double y0, double x1, double y1, double width);
void     lxp_xbm_rect(LxpXbm *xbm, int x, int y, int w, int h, int line);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(LxpXbm, lxp_xbm_free)
