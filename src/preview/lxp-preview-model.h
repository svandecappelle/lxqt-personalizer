/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-color.h"
#include "lxp-theme.h"
#include "lxp-xbm.h"

/* Openbox title bar buttons that can have their own bitmap and colors. */
typedef enum {
    LXP_BUTTON_CLOSE, LXP_BUTTON_MAX, LXP_BUTTON_ICONIFY, LXP_BUTTON_SHADE,
    LXP_BUTTON_DESK, LXP_BUTTON_COUNT,
} LxpButton;

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
    LxpColor button_kind_fg[LXP_BUTTON_COUNT];  /* per button, else button_fg */
    LxpColor border;
    LxpColor handle;
    LxpColor client;             /* client padding area */
} LxpDecoration;

/* A panel task button as an installed LXQt theme styles it. */
typedef struct {
    gboolean has_bg, has_text, has_edge;
    LxpColor bg, text, edge;
    char     edge_side;          /* 't', 'b', 'l', 'r', or 'a' (all around) */
} LxpPanelTaskStyle;

typedef struct {
    char    *name;               /* theme display name */

    /* Qt palette (LXQt) */
    LxpColor window, window_text, base, alt_base, text;
    LxpColor button, button_text, highlight, highlighted_text, link;
    char    *font_family;
    double   font_size;          /* points */
    char    *icon_theme;
    int      widget_radius;      /* buttons, fields, selections */

    /* Openbox */
    gboolean      has_openbox;   /* themerc found */
    LxpDecoration active, inactive;
    int           border_width, padding_width, padding_height;
    int           handle_width, client_padding;
    LxpXbm       *button_bitmaps[LXP_BUTTON_COUNT];  /* NULL: drawn glyph */
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
    char    *panel_style;        /* task buttons: underline, pills, flat, theme */
    int      panel_size;
    int      panel_icon_size;
    double   panel_opacity;      /* 0..1 */
    int      panel_width;        /* percent, or pixels when !panel_width_percent */
    gboolean panel_width_percent;
    int      panel_alignment;    /* -1 start, 0 center, 1 end */
    gboolean panel_reserve_space;
    LxpColor panel_bg, panel_text;
    /* from an installed LXQt theme's stylesheet (panel_style "theme") */
    gboolean panel_gradient;     /* panel_bg to panel_bg_to, across the panel */
    LxpColor panel_bg_to;
    LxpPanelTaskStyle panel_task, panel_task_active;

    /* Compositor (picom); defaults are picom's */
    int      corner_radius;
    gboolean shadow;
    int      shadow_radius;
    double   shadow_opacity;
    int      shadow_offset_x, shadow_offset_y;
    double   inactive_opacity;   /* 0..1 */
    gboolean blur;
    int      blur_strength;      /* 1..20 */

    /* Desktop */
    char    *wallpaper;          /* absolute path or NULL */
    char    *wallpaper_mode;     /* pcmanfm-qt mode */
    LxpColor desktop_bg;
} LxpPreviewModel;

LxpPreviewModel *lxp_preview_model_new(const LxpTheme *theme);
void             lxp_preview_model_free(LxpPreviewModel *model);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(LxpPreviewModel, lxp_preview_model_free)
