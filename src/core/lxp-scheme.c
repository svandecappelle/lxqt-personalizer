/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-scheme.h"

#include "lxp-error.h"
#include "lxp-fileutil.h"
#include "lxp-iconcolor.h"
#include "lxp-ini.h"
#include "lxp-paths.h"
#include "lxp-kvantum-gen.h"
#include "lxp-terminal-gen.h"
#include "lxp-recolor.h"
#include "lxp-template.h"
#include "lxp-xbm.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ASSET_MARKER ".lxp-asset"

/* QColor "Invalid": let the generated LXQt theme style the panel. */
#define QCOLOR_DEFAULT "@Variant(\\0\\0\\0\\x43\\0\\xff\\xff\\0\\0\\0\\0\\0\\0\\0\\0)"

/* --- scheme <-> theme.ini ------------------------------------------------ */

static gboolean get_color(const LxpTheme *theme, const char *key, LxpColor *c,
                          gboolean required, GError **error)
{
    g_autofree char *value = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, key);

    if (value && lxp_color_parse(value, c))
        return TRUE;
    if (required)
        g_set_error(error, LXP_ERROR, LXP_ERROR_INVALID,
                    value ? "Invalid [Scheme] %s color “%s”" : "Missing [Scheme] %s color%s",
                    key, value ? value : "");
    return FALSE;
}

gboolean lxp_scheme_load(const LxpTheme *theme, LxpScheme *s, GError **error)
{
    memset(s, 0, sizeof(*s));
    if (!get_color(theme, "background", &s->background, TRUE, error) ||
        !get_color(theme, "surface", &s->surface, TRUE, error) ||
        !get_color(theme, "foreground", &s->foreground, TRUE, error) ||
        !get_color(theme, "accent", &s->accent, TRUE, error))
        return FALSE;
    s->has_accent_text = get_color(theme, "accent_text", &s->accent_text, FALSE, NULL);
    s->has_title = get_color(theme, "title", &s->title, FALSE, NULL);
    s->has_panel = get_color(theme, "panel", &s->panel, FALSE, NULL);
    s->has_link = get_color(theme, "link", &s->link, FALSE, NULL);
    for (int i = 0; i < LXP_ANSI_COLORS; i++) {
        g_autofree char *key = g_strdup_printf("ansi%d", i);
        s->has_ansi[i] = get_color(theme, key, &s->ansi[i], FALSE, NULL);
    }
    s->font = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "font");
    s->title_font = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "title_font");
    s->gtk_font = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "gtk_font");
    s->prefer_dark = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "prefer_dark");
    s->title_style = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "title_style");
    s->panel_style = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "panel_style");
    s->icons_base = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "icons_base");
    {
        g_autofree char *folders = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "folder_color");
        s->folder_color = folders && g_ascii_strcasecmp(folders, "true") == 0;
    }
    s->title_align = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "title_align");
    s->buttons = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "buttons");
    {
        g_autofree char *padding = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "title_padding");
        g_autofree char *border = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "border_width");
        s->title_padding = padding ? CLAMP(atoi(padding), 1, 16) : 0;
        g_autofree char *wr = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "widget_radius");
        s->has_widget_radius = wr != NULL;
        s->widget_radius = wr ? CLAMP(atoi(wr), 0, 16) : 6;
        s->has_border_width = border != NULL;
        s->border_width = border ? CLAMP(atoi(border), 0, 4) : 1;
    }
    s->wallpaper = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "wallpaper");
    {
        g_autofree char *seed = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "wallpaper_seed");
        s->wallpaper_seed = seed ? (guint32)g_ascii_strtoull(seed, NULL, 10) : 1;
    }
    s->style = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "style");
    {
        g_auto(GStrv) parts = lxp_theme_get_string_list(theme, LXP_SCHEME_GROUP, "generate");
        gboolean all = parts == NULL;

        s->keep_openbox = !all && !g_strv_contains((const char *const *)parts, "openbox");
        s->keep_gtk = !all && !g_strv_contains((const char *const *)parts, "gtk");
        s->keep_lxqt = !all && !g_strv_contains((const char *const *)parts, "lxqt");
        s->keep_kvantum = !all && !g_strv_contains((const char *const *)parts, "kvantum");
        s->keep_terminal = !all && !g_strv_contains((const char *const *)parts, "terminal");
    }
    return TRUE;
}

static void set_color(LxpTheme *theme, const char *key, const LxpColor *c, gboolean set)
{
    g_autofree char *hex = set ? lxp_color_to_hex(c) : NULL;
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, key, hex);
}

void lxp_scheme_save(const LxpScheme *s, LxpTheme *theme)
{
    set_color(theme, "background", &s->background, TRUE);
    set_color(theme, "surface", &s->surface, TRUE);
    set_color(theme, "foreground", &s->foreground, TRUE);
    set_color(theme, "accent", &s->accent, TRUE);
    set_color(theme, "accent_text", &s->accent_text, s->has_accent_text);
    set_color(theme, "title", &s->title, s->has_title);
    set_color(theme, "panel", &s->panel, s->has_panel);
    set_color(theme, "link", &s->link, s->has_link);
    for (int i = 0; i < LXP_ANSI_COLORS; i++) {
        g_autofree char *key = g_strdup_printf("ansi%d", i);
        set_color(theme, key, &s->ansi[i], s->has_ansi[i]);
    }
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "font", s->font && *s->font ? s->font : NULL);
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "title_font",
                         s->title_font && *s->title_font ? s->title_font : NULL);
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "gtk_font",
                         s->gtk_font && *s->gtk_font ? s->gtk_font : NULL);
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "prefer_dark",
                         s->prefer_dark && *s->prefer_dark ? s->prefer_dark : NULL);
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "title_style",
                         s->title_style && *s->title_style ? s->title_style : NULL);
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "panel_style",
                         s->panel_style && *s->panel_style ? s->panel_style : NULL);
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "folder_color",
                         s->folder_color ? "true" : NULL);
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "icons_base",
                         s->icons_base && *s->icons_base ? s->icons_base : NULL);
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "title_align",
                         s->title_align && *s->title_align ? s->title_align : NULL);
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "buttons",
                         s->buttons && *s->buttons ? s->buttons : NULL);
    {
        g_autofree char *padding = s->title_padding > 0
            ? g_strdup_printf("%d", s->title_padding) : NULL;
        g_autofree char *border = s->has_border_width
            ? g_strdup_printf("%d", s->border_width) : NULL;
        lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "title_padding", padding);
        lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "border_width", border);
    }
    {
        g_autofree char *wr = s->has_widget_radius ? g_strdup_printf("%d", s->widget_radius)
                                                   : NULL;
        lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "widget_radius", wr);
    }
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "wallpaper",
                         s->wallpaper && *s->wallpaper ? s->wallpaper : NULL);
    if (s->wallpaper && *s->wallpaper) {
        g_autofree char *seed = g_strdup_printf("%u", s->wallpaper_seed);
        lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "wallpaper_seed", seed);
    } else {
        lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "wallpaper_seed", NULL);
    }
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "style",
                         s->style && *s->style ? s->style : NULL);

    if (!s->keep_openbox && !s->keep_gtk && !s->keep_lxqt && !s->keep_kvantum &&
        !s->keep_terminal) {
        lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "generate", NULL);
    } else {
        const char *parts[6];
        int n = 0;

        if (!s->keep_openbox)
            parts[n++] = "openbox";
        if (!s->keep_gtk)
            parts[n++] = "gtk";
        if (!s->keep_lxqt)
            parts[n++] = "lxqt";
        if (!s->keep_kvantum)
            parts[n++] = "kvantum";
        if (!s->keep_terminal)
            parts[n++] = "terminal";
        parts[n] = NULL;
        lxp_theme_set_string_list(theme, LXP_SCHEME_GROUP, "generate", parts);
    }
}

gboolean lxp_scheme_from_palette(const LxpTheme *theme, LxpScheme *s)
{
    static const char *const keys[] = {
        "window_color", "base_color", "text_color", "highlight_color",
    };
    LxpColor *targets[] = { &s->background, &s->surface, &s->foreground, &s->accent };

    memset(s, 0, sizeof(*s));
    for (gsize i = 0; i < G_N_ELEMENTS(keys); i++) {
        g_autofree char *v = lxp_theme_get_string(theme, "LXQt Palette", keys[i]);
        if (!v || !lxp_qt_color_parse(v, targets[i]))
            return FALSE;
    }
    {
        g_autofree char *v = lxp_theme_get_string(theme, "LXQt Palette",
                                                  "highlighted_text_color");
        s->has_accent_text = v && lxp_qt_color_parse(v, &s->accent_text);
    }
    return TRUE;
}

void lxp_scheme_clear(LxpScheme *s)
{
    g_clear_pointer(&s->font, g_free);
    g_clear_pointer(&s->title_font, g_free);
    g_clear_pointer(&s->gtk_font, g_free);
    g_clear_pointer(&s->prefer_dark, g_free);
    g_clear_pointer(&s->wallpaper, g_free);
    g_clear_pointer(&s->title_style, g_free);
    g_clear_pointer(&s->panel_style, g_free);
    g_clear_pointer(&s->icons_base, g_free);
    g_clear_pointer(&s->title_align, g_free);
    g_clear_pointer(&s->buttons, g_free);
    g_clear_pointer(&s->style, g_free);
}

/* --- derived colors ----------------------------------------------------- */

static LxpColor hex(guint32 v)
{
    LxpColor c = { ((v >> 16) & 0xff) / 255.0, ((v >> 8) & 0xff) / 255.0,
                   (v & 0xff) / 255.0, 1 };
    return c;
}

/* Text color for <bg>: <preferred> if readable, else black or white. */
static LxpColor readable_on(const LxpColor *bg, const LxpColor *preferred)
{
    LxpColor white = hex(0xffffff), black = hex(0x1a1a1a);

    if (preferred && lxp_color_contrast(bg, preferred) >= 4.5)
        return *preferred;
    return lxp_color_contrast(bg, &white) >= lxp_color_contrast(bg, &black) ? white : black;
}

void lxp_scheme_derive(const LxpScheme *s, LxpSchemeColors *c)
{
    LxpColor black = hex(0x000000);
    double h, sat, l;

    c->dark = lxp_color_luminance(&s->background) < lxp_color_luminance(&s->foreground);
    c->bg = s->background;
    c->surface = s->surface;
    c->fg = s->foreground;
    c->bg_alt = lxp_color_mix(&c->bg, &c->fg, 0.07);
    c->border = lxp_color_mix(&c->bg, &c->fg, 0.20);
    c->fg_dim = lxp_color_mix(&c->fg, &c->bg, 0.45);

    c->accent = s->accent;
    c->accent_fg = s->has_accent_text ? s->accent_text : readable_on(&c->accent, NULL);

    if (s->has_link) {
        c->link = s->link;
        lxp_color_to_hsl(&c->link, &h, &sat, &l);
        c->link_visited = lxp_color_from_hsl(h + 45, sat, l, 1);
    } else {
        lxp_color_to_hsl(&c->accent, &h, &sat, &l);
        c->link = lxp_color_from_hsl(h, sat, c->dark ? MAX(l, 0.68) : MIN(l, 0.40), 1);
        c->link_visited = lxp_color_from_hsl(h + 45, sat, c->dark ? MAX(l, 0.68) : MIN(l, 0.40), 1);
    }

    if (s->has_title) {
        c->title_active = s->title;
        c->title_active_fg = readable_on(&c->title_active, &c->fg);
        c->title_active_hover = lxp_color_mix(&c->title_active_fg, &c->title_active, 0.3);
    } else {
        c->title_active = c->bg_alt;
        c->title_active_fg = c->fg;
        c->title_active_hover = c->accent;
    }
    c->title_inactive = c->bg;
    c->title_inactive_fg = c->fg_dim;

    if (s->has_panel)
        c->panel = s->panel;
    else
        c->panel = c->dark ? lxp_color_mix(&c->bg, &black, 0.30)
                           : lxp_color_mix(&c->bg, &c->fg, 0.10);
    c->panel_fg = readable_on(&c->panel, &c->fg);
    c->panel_hover = lxp_color_mix(&c->panel, &c->panel_fg, 0.14);

    lxp_terminal_palette(c, c->ansi);
    for (int i = 0; i < LXP_ANSI_COLORS; i++)
        if (s->has_ansi[i])
            c->ansi[i] = s->ansi[i];
}

/* --- generation --------------------------------------------------------- */

static void add_color(GHashTable *vars, const char *name, const LxpColor *c)
{
    g_hash_table_insert(vars, g_strdup(name), lxp_color_to_hex(c));
    g_hash_table_insert(vars, g_strconcat(name, "_rgb", NULL), lxp_color_to_rgb_triplet(c));
}

static GHashTable *template_vars(const char *name, const LxpSchemeColors *c)
{
    GHashTable *vars = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    g_hash_table_insert(vars, g_strdup("name"), g_strdup(name));
    add_color(vars, "bg", &c->bg);
    add_color(vars, "bg_alt", &c->bg_alt);
    add_color(vars, "surface", &c->surface);
    add_color(vars, "fg", &c->fg);
    add_color(vars, "fg_dim", &c->fg_dim);
    add_color(vars, "border", &c->border);
    add_color(vars, "accent", &c->accent);
    add_color(vars, "accent_fg", &c->accent_fg);
    add_color(vars, "link", &c->link);
    add_color(vars, "title_active", &c->title_active);
    add_color(vars, "title_active_fg", &c->title_active_fg);
    add_color(vars, "title_active_hover", &c->title_active_hover);
    add_color(vars, "title_inactive", &c->title_inactive);
    add_color(vars, "title_inactive_fg", &c->title_inactive_fg);
    add_color(vars, "panel", &c->panel);
    add_color(vars, "panel_fg", &c->panel_fg);
    add_color(vars, "panel_hover", &c->panel_hover);
    return vars;
}

static void add_text(GHashTable *vars, const char *name, const char *value)
{
    g_hash_table_insert(vars, g_strdup(name), g_strdup(value));
}

static void add_int(GHashTable *vars, const char *name, int value)
{
    g_hash_table_insert(vars, g_strdup(name), g_strdup_printf("%d", value));
}

/* Per-button themerc rules for the generated button sets. */
static char *button_rules(const LxpScheme *s, const LxpSchemeColors *c)
{
    GString *r = g_string_new(NULL);

    if (g_strcmp0(s->buttons, "dots") == 0) {
        /* close, minimize, maximize as red, yellow and green dots */
        static const struct { const char *button, *normal, *hover, *pressed; } dots[] = {
            { "close",   "#ff5f57", "#ff7b75", "#e0443e" },
            { "iconify", "#febc2e", "#ffd062", "#dea123" },
            { "max",     "#28c840", "#4cd964", "#1aab29" },
        };
        LxpColor idle = lxp_color_mix(&c->title_inactive, &c->title_inactive_fg, 0.35);
        g_autofree char *idle_hex = lxp_color_to_hex(&idle);
        g_autofree char *other = lxp_color_to_hex(&c->title_active_fg);

        for (gsize i = 0; i < G_N_ELEMENTS(dots); i++) {
            g_string_append_printf(r, "window.active.button.%s.unpressed.image.color: %s\n"
                                      "window.active.button.%s.hover.image.color: %s\n"
                                      "window.active.button.%s.pressed.image.color: %s\n",
                                   dots[i].button, dots[i].normal, dots[i].button,
                                   dots[i].hover, dots[i].button, dots[i].pressed);
        }
        g_string_append_printf(r, "window.active.button.max.toggled.unpressed.image.color: %s\n"
                                  "window.active.button.max.toggled.hover.image.color: %s\n"
                                  "window.active.button.unpressed.image.color: %s\n"
                                  "window.inactive.button.unpressed.image.color: %s\n"
                                  "window.inactive.button.hover.image.color: %s\n",
                               dots[2].normal, dots[2].hover, other, idle_hex, idle_hex);
    } else if (g_strcmp0(s->buttons, "minimal") == 0) {
        /* quiet glyphs, a soft square on hover, red behind close */
        LxpColor hover = lxp_color_mix(&c->title_active, &c->title_active_fg, 0.12);
        LxpColor pressed = lxp_color_mix(&c->title_active, &c->title_active_fg, 0.22);
        g_autofree char *hover_hex = lxp_color_to_hex(&hover);
        g_autofree char *pressed_hex = lxp_color_to_hex(&pressed);
        g_autofree char *fg = lxp_color_to_hex(&c->title_active_fg);

        g_string_append_printf(r,
            "window.active.button.hover.bg: Flat Solid\n"
            "window.active.button.hover.bg.color: %s\n"
            "window.active.button.hover.image.color: %s\n"
            "window.active.button.pressed.bg: Flat Solid\n"
            "window.active.button.pressed.bg.color: %s\n"
            "window.active.button.pressed.image.color: %s\n"
            "window.active.button.close.hover.bg: Flat Solid\n"
            "window.active.button.close.hover.bg.color: #c42b1c\n"
            "window.active.button.close.hover.image.color: #ffffff\n"
            "window.active.button.close.pressed.bg: Flat Solid\n"
            "window.active.button.close.pressed.bg.color: #b02717\n"
            "window.active.button.close.pressed.image.color: #ffffff\n",
            hover_hex, fg, pressed_hex, fg);
    }
    return g_string_free(r, FALSE);
}

/* Task button rules of the generated LXQt panel theme. */
static void add_panel_style(GHashTable *vars, const LxpScheme *s, const LxpSchemeColors *c)
{
    g_autofree char *accent = lxp_color_to_hex(&c->accent);
    g_autofree char *accent_rgb = lxp_color_to_rgb_triplet(&c->accent);
    g_autofree char *fg_rgb = lxp_color_to_rgb_triplet(&c->panel_fg);
    int radius = s->has_widget_radius ? s->widget_radius : 6;

    if (g_strcmp0(s->panel_style, "pills") == 0) {
        add_int(vars, "panel_radius", 10);
        add_text(vars, "task_margin", "4px 2px");
        g_hash_table_insert(vars, g_strdup("task_on_rules"),
                            g_strdup_printf("    background-color: rgba(%s, 0.55);", accent_rgb));
        g_hash_table_insert(vars, g_strdup("task_on_hover_rules"),
                            g_strdup_printf("    background-color: rgba(%s, 0.70);", accent_rgb));
    } else if (g_strcmp0(s->panel_style, "flat") == 0) {
        add_int(vars, "panel_radius", 0);
        add_text(vars, "task_margin", "0");
        g_hash_table_insert(vars, g_strdup("task_on_rules"),
                            g_strdup_printf("    background-color: rgba(%s, 0.16);", fg_rgb));
        g_hash_table_insert(vars, g_strdup("task_on_hover_rules"),
                            g_strdup_printf("    background-color: rgba(%s, 0.24);", fg_rgb));
    } else {
        add_int(vars, "panel_radius", MIN(radius, 6));
        add_text(vars, "task_margin", "2px 1px");
        g_hash_table_insert(vars, g_strdup("task_on_rules"),
                            g_strdup_printf("    background-color: rgba(%s, 0.25);\n"
                                            "    border-bottom: 2px solid %s;", accent_rgb, accent));
        g_hash_table_insert(vars, g_strdup("task_on_hover_rules"),
                            g_strdup_printf("    background-color: rgba(%s, 0.40);", accent_rgb));
    }
    add_int(vars, "menu_item_radius", MAX(radius - 2, 0));
}

static void add_window_style(GHashTable *vars, const LxpScheme *s, const LxpSchemeColors *c)
{
    gboolean gradient = g_strcmp0(s->title_style, "gradient") == 0;
    LxpColor shade = { 0, 0, 0, 1 };
    LxpColor active_to = lxp_color_mix(&c->title_active, &shade, c->dark ? 0.18 : 0.08);
    LxpColor inactive_to = lxp_color_mix(&c->title_inactive, &shade, c->dark ? 0.12 : 0.05);
    int padding = s->title_padding > 0 ? s->title_padding : 4;
    const char *align = s->title_align;

    if (g_strcmp0(align, "left") != 0 && g_strcmp0(align, "right") != 0)
        align = "center";
    add_int(vars, "border_width", s->has_border_width ? s->border_width : 1);
    add_int(vars, "padding_height", padding);
    add_int(vars, "padding_width", MAX(6, padding + 2));
    add_text(vars, "title_texture", gradient ? "Flat Gradient Vertical" : "Flat Solid");
    add_color(vars, "title_active_to", gradient ? &active_to : &c->title_active);
    add_color(vars, "title_inactive_to", gradient ? &inactive_to : &c->title_inactive);
    add_text(vars, "title_align", align);
    g_hash_table_insert(vars, g_strdup("button_rules"), button_rules(s, c));
}

/* openbox-3/<name>.xbm button bitmaps for the "minimal" and "dots" sets. */
static gboolean write_buttons(const char *dir, const char *set, GError **error)
{
    static const char *const names[] = {
        "close", "max", "max_toggled", "iconify", "desk", "desk_toggled",
        "shade", "shade_toggled",
    };
    gboolean dots = g_strcmp0(set, "dots") == 0;
    int size = dots ? 12 : 10;

    if (!dots && g_strcmp0(set, "minimal") != 0)
        return TRUE;  /* classic: Openbox's own buttons */

    for (gsize i = 0; i < G_N_ELEMENTS(names); i++) {
        g_autoptr(LxpXbm) xbm = lxp_xbm_new(size, size);
        g_autofree char *file = g_strconcat(names[i], ".xbm", NULL);
        g_autofree char *path = g_build_filename(dir, "openbox-3", file, NULL);
        g_autofree char *text = NULL;
        const char *n = names[i];

        if (dots) {
            lxp_xbm_fill_circle(xbm, size / 2.0, size / 2.0, size / 2.0);
        } else if (g_str_equal(n, "close")) {
            lxp_xbm_line(xbm, 1.5, 1.5, 8.5, 8.5, 1.3);
            lxp_xbm_line(xbm, 8.5, 1.5, 1.5, 8.5, 1.3);
        } else if (g_str_equal(n, "max")) {
            lxp_xbm_rect(xbm, 1, 1, 8, 8, 1);
        } else if (g_str_equal(n, "max_toggled")) {
            lxp_xbm_rect(xbm, 3, 0, 7, 7, 1);
            lxp_xbm_rect(xbm, 0, 3, 7, 7, 1);
        } else if (g_str_equal(n, "iconify")) {
            lxp_xbm_line(xbm, 1, 5.5, 9, 5.5, 1.1);
        } else if (g_str_has_prefix(n, "desk")) {
            lxp_xbm_fill_circle(xbm, 5, 5, g_str_has_suffix(n, "toggled") ? 3.2 : 2);
        } else {  /* shade */
            lxp_xbm_line(xbm, 1, 2.5, 9, 2.5, 1.1);
            if (g_str_has_suffix(n, "toggled"))
                lxp_xbm_line(xbm, 1, 7.5, 9, 7.5, 1.1);
        }
        text = lxp_xbm_to_text(xbm, n);
        if (!lxp_write_atomic(path, text, -1, 0644, error))
            return FALSE;
    }
    return TRUE;
}

static gboolean write_template(const char *tmpl, GHashTable *vars, const char *dir,
                               const char *relpath, GError **error)
{
    g_autofree char *text = lxp_template_expand_resource(tmpl, vars, error);
    g_autofree char *path = g_build_filename(dir, relpath, NULL);

    return text && lxp_write_atomic(path, text, -1, 0644, error);
}

static char *find_gtk_library(int major)
{
    static const char *const dirs[] = {
        "/usr/lib", "/usr/lib64", "/usr/lib/x86_64-linux-gnu",
        "/usr/lib/aarch64-linux-gnu", "/usr/local/lib", "/lib/x86_64-linux-gnu", NULL,
    };
    const char *name = major == 3 ? "libgtk-3.so.0" : "libgtk-4.so.1";

    for (const char *const *d = dirs; *d; d++) {
        char *path = g_build_filename(*d, name, NULL);
        if (g_file_test(path, G_FILE_TEST_EXISTS))
            return path;
        g_free(path);
    }
    return NULL;
}

/*
 * The stylesheet GTK <major> uses by default, extracted from the
 * installed library with gresource(1). NULL when unavailable.
 */
static char *extract_gtk_base_css(int major, gboolean dark)
{
    const char *base = major == 3 ? "/org/gtk/libgtk/theme/Adwaita/"
                                  : "/org/gtk/libgtk/theme/Default/";
    const char *file = major == 3 ? (dark ? "gtk-contained-dark.css" : "gtk-contained.css")
                                  : (dark ? "Default-dark.css" : "Default-light.css");
    g_autofree char *lib = find_gtk_library(major);
    g_autofree char *resource = g_strconcat(base, file, NULL);
    g_autofree char *out = NULL;
    g_autofree char *assets = NULL;
    g_autofree char *absolute = NULL;
    const char *argv[] = { "gresource", "extract", lib, resource, NULL };
    int status;

    if (!lib ||
        !g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
                      NULL, NULL, &out, NULL, &status, NULL) ||
        !g_spawn_check_wait_status(status, NULL) || !out || !*out)
        return NULL;

    /* Relative asset URLs only resolve inside GTK's own resources. */
    assets = g_strconcat("url(\"resource://", base, "assets/", NULL);
    {
        g_auto(GStrv) parts = g_strsplit(out, "url(\"assets/", -1);
        absolute = g_strjoinv(assets, parts);
    }
    return g_steal_pointer(&absolute);
}

/* Extracting takes a process spawn; the editor regenerates on every change. */
static char *gtk_base_css(int major, gboolean dark)
{
    static GHashTable *cache;
    G_LOCK_DEFINE_STATIC(cache);
    g_autofree char *key = g_strdup_printf("%d-%d", major, dark);
    char *css;

    G_LOCK(cache);
    if (!cache)
        cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    if (!g_hash_table_lookup_extended(cache, key, NULL, (gpointer *)&css)) {
        css = extract_gtk_base_css(major, dark);
        g_hash_table_insert(cache, g_strdup(key), css);
    }
    css = g_strdup(css);
    G_UNLOCK(cache);
    return css;
}

/* Corners from the scheme, after GTK's own rules (linked groups keep theirs). */
static char *gtk_corners(int major, int radius)
{
    int popover = MIN(radius + 2, 12), tip = MIN(radius, 8);

    if (major == 3)
        return g_strdup_printf("\n/* corners from the scheme */\n"
                               "button, entry, spinbutton { border-radius: %dpx; }\n"
                               "popover.background { border-radius: %dpx; }\n"
                               "tooltip.background, tooltip { border-radius: %dpx; }\n",
                               radius, popover, tip);
    return g_strdup_printf("\n/* corners from the scheme */\n"
                           "button, entry, spinbutton, dropdown > button { border-radius: %dpx; }\n"
                           "popover > contents { border-radius: %dpx; }\n"
                           "tooltip { border-radius: %dpx; }\n",
                           radius, popover, tip);
}

static gboolean write_gtk_css(int major, const char *name, const LxpSchemeColors *c,
                              GHashTable *vars, int radius, const char *theme_dir,
                              GError **error)
{
    g_autofree char *base = gtk_base_css(major, c->dark);
    g_autofree char *css = NULL;
    g_autofree char *rel = g_strdup_printf("gtk-%d.0/gtk.css", major);
    g_autofree char *rel_dark = g_strdup_printf("gtk-%d.0/gtk-dark.css", major);
    g_autofree char *path = g_build_filename(theme_dir, rel, NULL);
    g_autofree char *path_dark = g_build_filename(theme_dir, rel_dark, NULL);
    LxpRecolorPalette from, to = { c->bg, c->surface, c->fg, c->accent };

    if (base && lxp_recolor_palette_from_gtk_css(base, &from)) {
        g_autofree char *recolored = lxp_recolor_css(base, &from, &to);
        g_autofree char *accent_fg = lxp_color_to_hex(&c->accent_fg);

        g_autofree char *corners = gtk_corners(major, radius);

        css = g_strdup_printf("/* %s — generated by lxqt-personalizer from GTK %d's own "
                              "stylesheet, regenerate instead of editing. */\n\n%s\n"
                              "@define-color theme_selected_fg_color %s;\n%s",
                              name, major, recolored, accent_fg, corners);
    } else {
        css = lxp_template_expand_resource("gtk-fallback.css", vars, error);
        if (!css)
            return FALSE;
    }
    return lxp_write_atomic(path, css, -1, 0644, error) &&
           lxp_write_atomic(path_dark, css, -1, 0644, error);
}

/* Marks <dir> as installable and owned by lxqt-personalizer. */
static gboolean write_marker(const char *dir, GError **error)
{
    g_autofree char *path = g_build_filename(dir, ASSET_MARKER, NULL);
    return lxp_write_atomic(path, "", 0, 0644, error);
}

char *lxp_scheme_asset_name(const LxpTheme *theme)
{
    g_autofree char *slug = lxp_theme_slugify(lxp_theme_get_name(theme));
    return g_strconcat("lxp-", slug, NULL);
}

/* "Noto Sans Bold 10" -> family "Noto Sans Bold", size 10 */
static void split_font(const char *font, char **family, double *size)
{
    const char *space = font ? strrchr(font, ' ') : NULL;
    char *end;
    double v = space ? g_ascii_strtod(space + 1, &end) : 0;

    if (space && v > 0 && *end == '\0') {
        *family = g_strndup(font, space - font);
        *size = v;
    } else {
        *family = g_strdup(font && *font ? font : "Sans");
        *size = 10;
    }
}

/* "Noto Sans Bold Italic" -> family "Noto Sans", bold, italic */
static char *strip_style_words(const char *family, gboolean *bold, gboolean *italic)
{
    g_auto(GStrv) words = g_strsplit(family, " ", -1);
    g_autoptr(GStrvBuilder) kept = g_strv_builder_new();
    g_auto(GStrv) out = NULL;

    *bold = *italic = FALSE;
    for (char **w = words; *w; w++) {
        if (g_ascii_strcasecmp(*w, "Bold") == 0)
            *bold = TRUE;
        else if (g_ascii_strcasecmp(*w, "Italic") == 0 || g_ascii_strcasecmp(*w, "Oblique") == 0)
            *italic = TRUE;
        else if (g_ascii_strcasecmp(*w, "Regular") != 0 && **w)
            g_strv_builder_add(kept, *w);
    }
    out = g_strv_builder_end(kept);
    return out[0] ? g_strjoinv(" ", out) : g_strdup("Sans");
}

/*
 * Openbox fonts: window titles and menu headers use title_font (or the
 * font in bold), menu items and on-screen displays the font.
 */
static void set_openbox_fonts(LxpTheme *theme, const LxpScheme *s)
{
    static const char *const places[] = {
        "ActiveWindow", "InactiveWindow", "MenuHeader", "MenuItem",
        "ActiveOnScreenDisplay", "InactiveOnScreenDisplay",
    };

    if (!(s->font && *s->font) && !(s->title_font && *s->title_font))
        return;
    for (gsize i = 0; i < G_N_ELEMENTS(places); i++) {
        gboolean title = i < 3;
        const char *spec = title && s->title_font && *s->title_font ? s->title_font : s->font;
        g_autofree char *raw = NULL, *family = NULL, *key = NULL, *size_text = NULL;
        gboolean bold, italic;
        double size;

        if (!spec || !*spec)
            continue;
        split_font(spec, &raw, &size);
        family = strip_style_words(raw, &bold, &italic);
        if (title && !(s->title_font && *s->title_font))
            bold = TRUE;  /* titles default to bold */
        key = g_strconcat("font_", places[i], NULL);
        size_text = g_strdup_printf("%g", size);
        {
            const char *values[] = { family, size_text, bold ? "Bold" : "Normal",
                                     italic ? "Italic" : "Normal", NULL };
            lxp_theme_set_string_list(theme, "Openbox", key, values);
        }
    }
}

static void set_theme_keys(LxpTheme *theme, const char *name, const LxpScheme *s,
                           const LxpSchemeColors *c)
{
    struct { const char *key; const LxpColor *color; } palette[] = {
        { "window_color", &c->bg },          { "window_text_color", &c->fg },
        { "base_color", &c->surface },       { "text_color", &c->fg },
        { "highlight_color", &c->accent },   { "highlighted_text_color", &c->accent_fg },
        { "link_color", &c->link },          { "link_visited_color", &c->link_visited },
    };

    if (!s->keep_lxqt)
        lxp_theme_set_string(theme, "LXQt", "theme", name);
    lxp_theme_set_string(theme, "LXQt", "style", s->style && *s->style ? s->style : "Fusion");
    if (!g_key_file_has_key(lxp_theme_get_keyfile(theme), "LXQt",
                            "icon_follow_color_scheme", NULL))
        lxp_theme_set_string(theme, "LXQt", "icon_follow_color_scheme", "true");
    lxp_theme_remove_group(theme, "LXQt Palette");
    for (gsize i = 0; i < G_N_ELEMENTS(palette); i++) {
        g_autofree char *v = lxp_color_to_hex(palette[i].color);
        lxp_theme_set_string(theme, "LXQt Palette", palette[i].key, v);
    }

    if (!s->keep_openbox)
        lxp_theme_set_string(theme, "Openbox", "theme", name);
    if (!s->keep_gtk)
        lxp_theme_set_string(theme, "GTK", "theme", name);
    lxp_theme_set_string(theme, "GTK", "prefer_dark",
                         s->prefer_dark ? s->prefer_dark : c->dark ? "true" : "false");

    /* the generated LXQt theme styles the panel */
    if (!s->keep_lxqt) {
        lxp_theme_set_string(theme, "Panel", "background-color", QCOLOR_DEFAULT);
        lxp_theme_set_string(theme, "Panel", "font-color", QCOLOR_DEFAULT);
    }

    if (s->font && *s->font) {
        g_autofree char *family = NULL;
        g_autofree char *qt = NULL;
        double size;

        split_font(s->font, &family, &size);
        qt = g_strdup_printf("%s,%g,-1,5,50,0,0,0,0,0", family, size);
        lxp_theme_set_string(theme, "LXQt", "font", qt);
    }
    if ((s->gtk_font && *s->gtk_font) || (s->font && *s->font))
        lxp_theme_set_string(theme, "GTK", "font",
                             s->gtk_font && *s->gtk_font ? s->gtk_font : s->font);
    set_openbox_fonts(theme, s);
}

/* Remove generated units of <kind> other than <keep> (e.g. after a rename). */
static gboolean remove_stale_units(const char *dir, const char *kind, const char *keep,
                                   GError **error)
{
    g_autofree char *parent = g_build_filename(dir, "data", kind, NULL);
    g_autoptr(GDir) d = g_dir_open(parent, 0, NULL);
    const char *name;

    while (d && (name = g_dir_read_name(d))) {
        g_autofree char *unit = g_build_filename(parent, name, NULL);
        g_autofree char *marker = g_build_filename(unit, ASSET_MARKER, NULL);

        if (g_str_has_prefix(name, "lxp-") && !g_str_equal(name, keep) &&
            g_file_test(marker, G_FILE_TEST_EXISTS) && !lxp_remove_tree(unit, error))
            return FALSE;
    }
    return TRUE;
}

/* The icon theme the desktop uses now: the highest lxqt.conf layer naming one. */
static char *desktop_icon_theme(void)
{
    g_autoptr(GPtrArray) layers = lxp_paths_find_config_layers("lxqt/lxqt.conf");

    for (guint i = layers->len; i > 0; i--) {
        g_autoptr(LxpIni) ini = lxp_ini_load(g_ptr_array_index(layers, i - 1), FALSE, NULL);
        const char *name = ini ? lxp_ini_get(ini, "General", "icon_theme") : NULL;

        if (name && *name)
            return g_strdup(name);
    }
    return NULL;
}

/*
 * data/icons/<name>: folders in the accent color, over the base icon
 * theme. Without SVG folder icons in the base, the theme keeps the base.
 */
static gboolean generate_folders(LxpTheme *theme, const char *dir, const char *name,
                                 const LxpScheme *s, const LxpSchemeColors *c, GError **error)
{
    g_autofree char *icons_dir = g_build_filename(dir, "data", "icons", name, NULL);
    g_autofree char *current = lxp_theme_get_string(theme, "LXQt", "icon_theme");
    g_autofree char *base = NULL;
    g_autoptr(GError) local = NULL;

    if (!lxp_remove_tree(icons_dir, error))
        return FALSE;

    /* the base: remembered, else what the theme or the desktop uses */
    if (s->icons_base && *s->icons_base)
        base = g_strdup(s->icons_base);
    else if (current && !g_str_has_prefix(current, "lxp-"))
        base = g_strdup(current);
    else
        base = desktop_icon_theme();

    if (!s->folder_color || !base) {
        /* back to the plain icon theme if the overlay was in use */
        if (base && g_strcmp0(current, name) == 0) {
            lxp_theme_set_string(theme, "LXQt", "icon_theme", base);
            lxp_theme_set_string(theme, "GTK", "icon_theme", base);
        }
        return TRUE;
    }

    if (!write_marker(icons_dir, error))
        return FALSE;
    if (!lxp_icons_folder_overlay(base, &c->accent, name, icons_dir, &local)) {
        if (!g_error_matches(local, LXP_ERROR, LXP_ERROR_NOT_FOUND)) {
            g_propagate_error(error, g_steal_pointer(&local));
            return FALSE;
        }
        return lxp_remove_tree(icons_dir, error);  /* nothing to recolor */
    }
    lxp_theme_set_string(theme, "LXQt", "icon_theme", name);
    lxp_theme_set_string(theme, "GTK", "icon_theme", name);
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "icons_base", base);
    return TRUE;
}

/* Same for config/<kind> (the Kvantum themes). */
static gboolean remove_stale_config_units(const char *dir, const char *kind, const char *keep,
                                          GError **error)
{
    g_autofree char *config = g_build_filename(dir, "config", NULL);
    g_autofree char *parent = g_build_filename(config, kind, NULL);
    g_autoptr(GDir) d = g_dir_open(parent, 0, NULL);
    const char *name;

    while (d && (name = g_dir_read_name(d))) {
        g_autofree char *unit = g_build_filename(parent, name, NULL);
        g_autofree char *marker = g_build_filename(unit, ASSET_MARKER, NULL);

        if (g_str_has_prefix(name, "lxp-") && !g_str_equal(name, keep) &&
            g_file_test(marker, G_FILE_TEST_EXISTS) && !lxp_remove_tree(unit, error))
            return FALSE;
    }
    return TRUE;
}

gboolean lxp_scheme_generate(LxpTheme *theme, const char *dir, GError **error)
{
    LxpScheme s;
    LxpSchemeColors c;
    g_autofree char *name = NULL;
    g_autofree char *themes_dir = NULL;
    g_autofree char *lxqt_dir = NULL;
    g_autofree char *kvantum_dir = NULL;
    gboolean gen_kvantum;
    g_autoptr(GHashTable) vars = NULL;
    gboolean ok;

    if (!lxp_scheme_load(theme, &s, error))
        return FALSE;
    lxp_scheme_derive(&s, &c);
    name = lxp_scheme_asset_name(theme);
    vars = template_vars(name, &c);
    add_window_style(vars, &s, &c);
    add_panel_style(vars, &s, &c);

    kvantum_dir = g_build_filename(dir, "config", "Kvantum", name, NULL);
    gen_kvantum = s.style && g_ascii_strcasecmp(s.style, "kvantum") == 0 && !s.keep_kvantum;
    themes_dir = g_build_filename(dir, "data", "themes", name, NULL);
    lxqt_dir = g_build_filename(dir, "data", "lxqt", "themes", name, NULL);

    ok = remove_stale_units(dir, "themes", name, error) &&
         remove_stale_units(dir, "lxqt/themes", name, error) &&
         remove_stale_config_units(dir, "Kvantum", name, error) &&
         lxp_remove_tree(themes_dir, error) && lxp_remove_tree(lxqt_dir, error) &&
         lxp_remove_tree(kvantum_dir, error);

    /* data/qtermwidget6/color-schemes/<name>.colorscheme for QTerminal */
    if (ok) {
        g_autofree char *file = g_strconcat(name, ".colorscheme", NULL);
        g_autofree char *schemes = g_build_filename(dir, "data", "qtermwidget6", "color-schemes",
                                                    NULL);
        g_autofree char *path = g_build_filename(schemes, file, NULL);

        ok = lxp_remove_tree(schemes, error);
        if (ok && !s.keep_terminal) {
            g_autofree char *text = lxp_terminal_colorscheme(&c, lxp_theme_get_name(theme));
            ok = lxp_write_atomic(path, text, -1, 0644, error);
        }
    }

    /* config/Kvantum/<name>: rounded Qt widgets in the scheme's colors */
    if (ok && gen_kvantum)
        ok = write_marker(kvantum_dir, error) &&
             lxp_kvantum_generate(&c, s.widget_radius, name, kvantum_dir, error);

    /* themes/<name> holds the Openbox and GTK parts */
    if (ok && (!s.keep_openbox || !s.keep_gtk))
        ok = write_marker(themes_dir, error) &&
             write_template("index.theme", vars, themes_dir, "index.theme", error);
    if (ok && !s.keep_openbox)
        ok = write_template("themerc", vars, themes_dir, "openbox-3/themerc", error) &&
             write_buttons(themes_dir, s.buttons, error);
    if (ok && !s.keep_gtk)
        ok = write_gtk_css(3, name, &c, vars, s.widget_radius, themes_dir, error) &&
             write_gtk_css(4, name, &c, vars, s.widget_radius, themes_dir, error);
    if (ok && !s.keep_lxqt)
        ok = write_marker(lxqt_dir, error) &&
             write_template("lxqt-panel.qss", vars, lxqt_dir, "lxqt-panel.qss", error) &&
             write_template("lxqt-runner.qss", vars, lxqt_dir, "lxqt-runner.qss", error) &&
             write_template("lxqt-notificationd.qss", vars, lxqt_dir,
                            "lxqt-notificationd.qss", error);

    if (ok) {
        ok = remove_stale_units(dir, "icons", name, error) &&
             generate_folders(theme, dir, name, &s, &c, error);
    }
    if (ok) {
        set_theme_keys(theme, name, &s, &c);
        if (gen_kvantum)
            lxp_theme_set_string(theme, "Kvantum", "theme", name);
        if (!s.keep_terminal) {
            lxp_theme_set_string(theme, "Terminal", "color_scheme", name);
        } else {
            /* a scheme generated earlier is gone: leave the terminal as it is */
            g_autofree char *kept = lxp_theme_get_string(theme, "Terminal", "color_scheme");
            if (kept && g_str_has_prefix(kept, "lxp-"))
                lxp_theme_remove_group(theme, "Terminal");
        }
    }
    lxp_scheme_clear(&s);
    return ok;
}
