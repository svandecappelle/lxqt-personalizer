/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-color.h"
#include "lxp-theme.h"

/*
 * A color scheme ([Scheme] in theme.ini) from which complete themes are
 * generated:
 *
 *   [Scheme]
 *   background=#2e3440     window background
 *   surface=#3b4252        views, entries, lists
 *   foreground=#eceff4     text
 *   accent=#88c0d0         selection, highlights
 *   accent_text=#2e3440    optional, text on accent (else best contrast)
 *   title=#5e81ac          optional, active title bar (else neutral)
 *   panel=#242933          optional, panel background (else derived)
 *   font=Noto Sans 10      optional
 *   title_font=Noto Sans Bold 10   optional window title font (else font, bold)
 *   gtk_font=Cantarell 11  optional font of GTK applications (else font)
 *   prefer_dark=true       optional dark GTK variant (else from the colors)
 *   link=#5e81ac           optional link color (else derived from accent)
 *   wallpaper=blobs        optional generated wallpaper style (gradient,
 *                          blobs, waves, geometric) and wallpaper_seed=N
 *   title_style=gradient   window title bars: flat (default) or gradient
 *   title_padding=6        space above and below the title (title height)
 *   border_width=1         window border, 0-4
 *   title_align=center     left, center or right
 *   buttons=dots           classic (Openbox's own), minimal (thin lines,
 *                          red close on hover) or dots (colored circles)
 *   panel_style=pills      task buttons of the generated LXQt theme:
 *                          underline (default), pills or flat
 *   folder_color=true      folder icons of the icon theme (icons_base=, else
 *                          the theme's or desktop's icon theme) recolored
 *                          to the accent, as a generated overlay icon theme
 *   widget_radius=6        corners of buttons, fields and selections in the
 *                          generated Kvantum theme (with style=kvantum)
 *   style=kvantum          optional Qt widget style (default Fusion, which
 *                          follows the palette everywhere)
 *   generate=openbox;gtk;lxqt;kvantum;terminal
 *                          optional: which themes are generated from the
 *                          colors (default all); for the others the
 *                          theme keeps the installed theme it names
 */

#define LXP_SCHEME_GROUP "Scheme"

typedef struct {
    LxpColor background, surface, foreground, accent;
    LxpColor accent_text, title, panel, link;
    gboolean has_accent_text, has_title, has_panel, has_link;
    char    *font;
    char    *title_font;
    char    *gtk_font;
    char    *prefer_dark;    /* "true", "false" or NULL (automatic) */
    char    *title_style;    /* "flat" or "gradient"; NULL: flat */
    int      title_padding;  /* 0: default */
    int      border_width;
    gboolean has_border_width;  /* else the default, 1 */
    char    *title_align;    /* NULL: center */
    char    *buttons;        /* NULL: classic */
    char    *wallpaper;      /* generated wallpaper style, or NULL */
    guint32  wallpaper_seed;
    char    *style;
    /* use the installed theme named in the theme instead of generating */
    gboolean keep_openbox, keep_gtk, keep_lxqt, keep_kvantum, keep_terminal;
    int      widget_radius;
    gboolean has_widget_radius;  /* else 6 */
    char    *panel_style;        /* NULL: underline */
    gboolean folder_color;
    char    *icons_base;         /* icon theme the folder overlay inherits */
} LxpScheme;

/* Every color the generated files use, derived from a scheme. */
typedef struct {
    gboolean dark;
    LxpColor bg, bg_alt, surface, fg, fg_dim, border;
    LxpColor accent, accent_fg, link, link_visited;
    LxpColor title_active, title_active_fg, title_active_hover;
    LxpColor title_inactive, title_inactive_fg;
    LxpColor panel, panel_fg, panel_hover;
} LxpSchemeColors;

gboolean lxp_scheme_load(const LxpTheme *theme, LxpScheme *scheme, GError **error);
void     lxp_scheme_save(const LxpScheme *scheme, LxpTheme *theme);
/* Seed a scheme from the theme's Qt palette (e.g. a captured desktop). */
gboolean lxp_scheme_from_palette(const LxpTheme *theme, LxpScheme *scheme);
void     lxp_scheme_clear(LxpScheme *scheme);

void     lxp_scheme_derive(const LxpScheme *scheme, LxpSchemeColors *colors);

/* Name of the generated Openbox/GTK/LXQt themes: "lxp-<slug>". */
char    *lxp_scheme_asset_name(const LxpTheme *theme);

/*
 * Generate Openbox, LXQt, GTK 3 and GTK 4 themes from the theme's
 * [Scheme] into <dir>/data/, and point the theme's LXQt, palette,
 * Openbox, GTK and panel settings at them. The caller saves the theme.
 */
gboolean lxp_scheme_generate(LxpTheme *theme, const char *dir, GError **error);
