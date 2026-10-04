/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-preview-model.h"

#include "lxp-themerc.h"

#include <stdlib.h>
#include <string.h>

static LxpColor rgb(guint32 v)
{
    LxpColor c = { ((v >> 16) & 0xff) / 255.0, ((v >> 8) & 0xff) / 255.0,
                   (v & 0xff) / 255.0, 1.0 };
    return c;
}

/* Black or white, whichever reads better on <bg>. */
static LxpColor contrast(const LxpColor *bg)
{
    return lxp_color_luminance(bg) > 0.4 ? rgb(0x000000) : rgb(0xffffff);
}

static gboolean contains_ci(const char *haystack, const char *needle)
{
    g_autofree char *h = haystack ? g_ascii_strdown(haystack, -1) : NULL;
    return h && strstr(h, needle);
}

/* "Noto Sans,10,-1,5,50,0,0,0,0,0" (QFont::toString) */
static void parse_qt_font(const char *text, char **family, double *size)
{
    g_auto(GStrv) parts = NULL;
    double pt;

    if (!text)
        return;
    parts = g_strsplit(text, ",", -1);
    if (parts[0] && *parts[0]) {
        g_free(*family);
        *family = g_strdup(parts[0]);
    }
    if (parts[0] && parts[1] && (pt = g_ascii_strtod(parts[1], NULL)) > 0)
        *size = pt;
}

static void palette_color(const LxpTheme *theme, const char *key, LxpColor *out)
{
    g_autofree char *value = lxp_theme_get_string(theme, "LXQt Palette", key);
    LxpColor c;

    if (value && lxp_qt_color_parse(value, &c))
        *out = c;
}

static void load_palette(LxpPreviewModel *m, const LxpTheme *theme)
{
    g_autofree char *font = lxp_theme_get_string(theme, "LXQt", "font");
    g_autofree char *icons = lxp_theme_get_string(theme, "LXQt", "icon_theme");

    /* Qt Fusion defaults */
    m->window = rgb(0xefefef);
    m->window_text = rgb(0x000000);
    m->base = rgb(0xffffff);
    m->text = rgb(0x000000);
    m->highlight = rgb(0x308cc6);
    m->highlighted_text = rgb(0xffffff);
    m->link = rgb(0x0000ff);

    palette_color(theme, "window_color", &m->window);
    palette_color(theme, "window_text_color", &m->window_text);
    palette_color(theme, "base_color", &m->base);
    palette_color(theme, "text_color", &m->text);
    palette_color(theme, "highlight_color", &m->highlight);
    palette_color(theme, "highlighted_text_color", &m->highlighted_text);
    palette_color(theme, "link_color", &m->link);

    m->button = m->window;
    m->button_text = m->window_text;
    palette_color(theme, "button_color", &m->button);
    palette_color(theme, "button_text_color", &m->button_text);
    m->alt_base = lxp_color_mix(&m->base, &m->text, 0.04);

    m->font_family = g_strdup("Sans");
    m->font_size = 10;
    parse_qt_font(font, &m->font_family, &m->font_size);
    m->icon_theme = g_strdup(icons ? icons : "hicolor");
}

static void load_decoration(LxpDecoration *d, const LxpThemerc *rc,
                            const char *state, const LxpPreviewModel *m,
                            gboolean active)
{
    g_autofree char *k_title_bg = g_strdup_printf("window.%s.title.bg", state);
    g_autofree char *k_title = g_strdup_printf("window.%s.title.bg.color", state);
    g_autofree char *k_title_to = g_strdup_printf("window.%s.title.bg.colorTo", state);
    g_autofree char *k_label_bg = g_strdup_printf("window.%s.label.bg", state);
    g_autofree char *k_label = g_strdup_printf("window.%s.label.bg.color", state);
    g_autofree char *k_text = g_strdup_printf("window.%s.label.text.color", state);
    g_autofree char *k_btn_bg = g_strdup_printf("window.%s.button.unpressed.bg", state);
    g_autofree char *k_btn = g_strdup_printf("window.%s.button.unpressed.bg.color", state);
    g_autofree char *k_img = g_strdup_printf("window.%s.button.unpressed.image.color", state);
    g_autofree char *k_border = g_strdup_printf("window.%s.border.color", state);
    g_autofree char *k_handle = g_strdup_printf("window.%s.handle.bg.color", state);
    g_autofree char *k_client = g_strdup_printf("window.%s.client.color", state);
    const char *texture = rc ? lxp_themerc_lookup(rc, k_title_bg) : NULL;

    if (!lxp_themerc_color(rc, &d->title_bg, k_title, NULL))
        d->title_bg = active ? rgb(0x4a6c94) : rgb(0xd6d6d6);

    d->title_gradient = contains_ci(texture, "gradient");
    if (!lxp_themerc_color(rc, &d->title_bg_to, k_title_to, NULL)) {
        LxpColor black = rgb(0x000000);
        d->title_bg_to = lxp_color_mix(&d->title_bg, &black, 0.15);
    }

    texture = rc ? lxp_themerc_lookup(rc, k_label_bg) : NULL;
    d->label_bg_visible = texture && !contains_ci(texture, "parentrelative");
    if (!lxp_themerc_color(rc, &d->label_bg, k_label, NULL))
        d->label_bg = d->title_bg;

    if (!lxp_themerc_color(rc, &d->label_text, k_text, NULL))
        d->label_text = contrast(&d->title_bg);

    texture = rc ? lxp_themerc_lookup(rc, k_btn_bg) : NULL;
    d->button_bg_visible = texture && !contains_ci(texture, "parentrelative");
    if (!lxp_themerc_color(rc, &d->button_bg, k_btn, NULL))
        d->button_bg = d->title_bg;
    if (!lxp_themerc_color(rc, &d->button_fg, k_img, NULL))
        d->button_fg = d->label_text;

    if (!lxp_themerc_color(rc, &d->border, k_border, "border.color", NULL))
        d->border = rgb(0x000000);
    if (!lxp_themerc_color(rc, &d->handle, k_handle, NULL))
        d->handle = d->title_bg;
    if (!lxp_themerc_color(rc, &d->client, k_client, NULL))
        d->client = m->window;
}

static void load_openbox(LxpPreviewModel *m, const LxpTheme *theme)
{
    g_autofree char *name = lxp_theme_get_string(theme, "Openbox", "theme");
    g_autofree char *layout = lxp_theme_get_string(theme, "Openbox", "title_layout");
    g_auto(GStrv) font = lxp_theme_get_string_list(theme, "Openbox", "font_ActiveWindow");
    g_autoptr(LxpThemerc) rc = name ? lxp_themerc_load_theme(name, NULL) : NULL;
    const char *justify;

    m->has_openbox = rc != NULL;
    load_decoration(&m->active, rc, "active", m, TRUE);
    load_decoration(&m->inactive, rc, "inactive", m, FALSE);

    m->border_width = lxp_themerc_int(rc, "border.width", 1);
    m->padding_width = lxp_themerc_int(rc, "padding.width", 3);
    m->padding_height = lxp_themerc_int(rc, "padding.height", m->padding_width);
    m->handle_width = lxp_themerc_int(rc, "window.handle.width", 6);
    m->client_padding = lxp_themerc_int(rc, "window.client.padding.width", 0);

    justify = rc ? lxp_themerc_lookup(rc, "window.label.text.justify") : NULL;
    m->title_justify = g_ascii_strdown(justify ? justify : "left", -1);
    m->title_layout = g_strdup(layout && *layout ? layout : "NLIMC");

    m->title_font_family = g_strdup(font && font[0] && *font[0] ? font[0] : "Sans");
    m->title_font_size = font && font[0] && font[1] ? g_ascii_strtod(font[1], NULL) : 0;
    if (m->title_font_size <= 0)
        m->title_font_size = 9;
    m->title_font_bold = font && font[0] && font[1] && font[2] &&
                         g_ascii_strcasecmp(font[2], "bold") == 0;

    if (!lxp_themerc_color(rc, &m->menu_bg, "menu.items.bg.color", NULL))
        m->menu_bg = m->window;
    if (!lxp_themerc_color(rc, &m->menu_text, "menu.items.text.color", NULL))
        m->menu_text = m->window_text;
    if (!lxp_themerc_color(rc, &m->menu_title_bg, "menu.title.bg.color", NULL))
        m->menu_title_bg = m->active.title_bg;
    if (!lxp_themerc_color(rc, &m->menu_title_text, "menu.title.text.color", NULL))
        m->menu_title_text = m->active.label_text;
    if (!lxp_themerc_color(rc, &m->menu_active_bg, "menu.items.active.bg.color", NULL))
        m->menu_active_bg = m->highlight;
    if (!lxp_themerc_color(rc, &m->menu_active_text, "menu.items.active.text.color", NULL))
        m->menu_active_text = m->highlighted_text;
    if (!lxp_themerc_color(rc, &m->menu_border, "menu.border.color", "border.color", NULL))
        m->menu_border = m->active.border;
    m->menu_border_width = lxp_themerc_int(rc, "menu.border.width", m->border_width);
}

static int theme_int(const LxpTheme *theme, const char *group, const char *key,
                     int fallback)
{
    g_autofree char *value = lxp_theme_get_string(theme, group, key);
    return value ? atoi(value) : fallback;
}

static void load_panel(LxpPreviewModel *m, const LxpTheme *theme)
{
    g_autofree char *position = lxp_theme_get_string(theme, "Panel", "position");
    g_autofree char *bg = lxp_theme_get_string(theme, "Panel", "background-color");
    g_autofree char *fg = lxp_theme_get_string(theme, "Panel", "font-color");

    m->panel_position = g_strdup(position ? position : "Bottom");
    m->panel_size = CLAMP(theme_int(theme, "Panel", "panelSize", 32), 16, 128);
    m->panel_icon_size = CLAMP(theme_int(theme, "Panel", "iconSize", 22), 8, 96);
    m->panel_opacity = CLAMP(theme_int(theme, "Panel", "opacity", 100), 0, 100) / 100.0;

    if (!lxp_qt_color_parse(bg, &m->panel_bg))
        m->panel_bg = m->window;
    if (!lxp_qt_color_parse(fg, &m->panel_text))
        m->panel_text = m->window_text;
}

static void load_wallpaper(LxpPreviewModel *m, const LxpTheme *theme)
{
    g_autofree char *file = lxp_theme_get_string(theme, "Wallpaper", "file");
    g_autofree char *mode = lxp_theme_get_string(theme, "Wallpaper", "mode");
    const char *dir = lxp_theme_get_dir(theme);

    if (file && !g_path_is_absolute(file) && dir)
        m->wallpaper = g_build_filename(dir, file, NULL);
    else
        m->wallpaper = g_steal_pointer(&file);
    if (m->wallpaper && !g_file_test(m->wallpaper, G_FILE_TEST_IS_REGULAR))
        g_clear_pointer(&m->wallpaper, g_free);

    m->wallpaper_mode = g_strdup(mode ? mode : "zoom");
    m->desktop_bg = rgb(0x2b3a4a);
}

LxpPreviewModel *lxp_preview_model_new(const LxpTheme *theme)
{
    LxpPreviewModel *m = g_new0(LxpPreviewModel, 1);

    m->name = g_strdup(lxp_theme_get_name(theme));
    load_palette(m, theme);
    load_openbox(m, theme);
    load_panel(m, theme);
    load_wallpaper(m, theme);
    return m;
}

void lxp_preview_model_free(LxpPreviewModel *m)
{
    if (!m)
        return;
    g_free(m->name);
    g_free(m->font_family);
    g_free(m->icon_theme);
    g_free(m->title_layout);
    g_free(m->title_justify);
    g_free(m->title_font_family);
    g_free(m->panel_position);
    g_free(m->wallpaper);
    g_free(m->wallpaper_mode);
    g_free(m);
}
