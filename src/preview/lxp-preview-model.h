/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-color.h"
#include "lxp-theme.h"

/*
 * Everything the preview needs, resolved from a theme: palette, Openbox
 * decorations (from the installed themerc), panel, wallpaper. Missing
 * values get the defaults the real programs would use.
 */

typedef struct {
    LxpColor title_bg;
    LxpColor title_bg_to;        /* gradient end, when title_gradient */
    gboolean title_gradient;
    LxpColor label_bg;
    gboolean label_bg_visible;   /* FALSE for "Parentrelative" */
    LxpColor label_text;
    LxpColor button_bg;
    gboolean button_bg_visible;
    LxpColor button_fg;
    LxpColor border;
    LxpColor handle;
    LxpColor client;             /* client padding area */
} LxpDecoration;

typedef struct {
    char    *name;               /* theme display name */

    /* Qt palette (LXQt) */
    LxpColor window, window_text, base, alt_base, text;
    LxpColor button, button_text, highlight, highlighted_text, link;
    char    *font_family;
    double   font_size;          /* points */
    char    *icon_theme;

    /* Openbox */
    gboolean      has_openbox;   /* themerc found */
    LxpDecoration active, inactive;
    int           border_width, padding_width, padding_height;
    int           handle_width, client_padding;
    char         *title_layout;  /* "NLIMC" */
    char         *title_justify; /* "left", "center", "right" */
    char         *title_font_family;
    double        title_font_size;
    gboolean      title_font_bold;
    LxpColor      menu_bg, menu_text, menu_title_bg, menu_title_text;
    LxpColor      menu_active_bg, menu_active_text, menu_border;
    int           menu_border_width;

    /* LXQt panel */
    char    *panel_position;     /* "Bottom", "Top", "Left", "Right" */
    int      panel_size;
    int      panel_icon_size;
    double   panel_opacity;      /* 0..1 */
    LxpColor panel_bg, panel_text;

    /* Compositor (picom); defaults are picom's */
    int      corner_radius;
    gboolean shadow;
    int      shadow_radius;
    double   shadow_opacity;
    int      shadow_offset_x, shadow_offset_y;

    /* Desktop */
    char    *wallpaper;          /* absolute path or NULL */
    char    *wallpaper_mode;     /* pcmanfm-qt mode */
    LxpColor desktop_bg;
} LxpPreviewModel;

LxpPreviewModel *lxp_preview_model_new(const LxpTheme *theme);
void             lxp_preview_model_free(LxpPreviewModel *model);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(LxpPreviewModel, lxp_preview_model_free)
