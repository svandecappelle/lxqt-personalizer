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
 *   style=kvantum          optional Qt widget style (default Fusion, which
 *                          follows the palette everywhere)
 *   generate=openbox;gtk;lxqt
 *                          optional: which themes are generated from the
 *                          colors (default all); for the others the
 *                          theme keeps the installed theme it names
 */

#define LXP_SCHEME_GROUP "Scheme"

typedef struct {
    LxpColor background, surface, foreground, accent;
    LxpColor accent_text, title, panel;
    gboolean has_accent_text, has_title, has_panel;
    char    *font;
    char    *style;
    /* use the installed theme named in the theme instead of generating */
    gboolean keep_openbox, keep_gtk, keep_lxqt;
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
