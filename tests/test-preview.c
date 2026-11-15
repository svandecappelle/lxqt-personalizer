/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-icons.h"
#include "lxp-preview-render.h"
#include "lxp-qss.h"
#include "test-util.h"

#include <string.h>

static void install_icon_themes(void)
{
    const char *data = g_get_user_data_dir();

    /* "child" inherits "parent"; lists are comma separated */
    write_file(data, "icons/child/index.theme",
               "[Icon Theme]\nName=Child\nInherits=parent,hicolor\n"
               "Directories=places/16,places/48,apps/scalable\n\n"
               "[places/16]\nSize=16\nType=Fixed\n\n"
               "[places/48]\nSize=48\nType=Fixed\n\n"
               "[apps/scalable]\nSize=48\nMinSize=8\nMaxSize=512\nType=Scalable\n");
    write_file(data, "icons/child/places/16/folder.png", "x");
    write_file(data, "icons/child/places/48/folder.png", "x");
    write_file(data, "icons/child/apps/scalable/terminal.svg", "x");
    write_file(data, "icons/parent/index.theme",
               "[Icon Theme]\nName=Parent\nDirectories=places/32\n\n"
               "[places/32]\nSize=32\nType=Threshold\n");
    write_file(data, "icons/parent/places/32/user-home.png", "x");
}

static void test_icons(void)
{
    const char *folder[] = { "folder", NULL };
    const char *terminal[] = { "terminal", NULL };
    const char *home[] = { "user-home", "folder", NULL };
    const char *missing[] = { "no-such-icon", NULL };
    const char *path;

    install_icon_themes();

    path = lxp_icon_lookup("child", folder, 48);
    g_assert_true(g_str_has_suffix(path, "child/places/48/folder.png"));
    path = lxp_icon_lookup("child", folder, 18);
    g_assert_true(g_str_has_suffix(path, "child/places/16/folder.png"));
    path = lxp_icon_lookup("child", terminal, 200);
    g_assert_true(g_str_has_suffix(path, "apps/scalable/terminal.svg"));
    /* like Qt: an exact name in an inherited theme beats a fallback name */
    path = lxp_icon_lookup("child", home, 32);
    g_assert_true(g_str_has_suffix(path, "parent/places/32/user-home.png"));
    path = lxp_icon_lookup("parent", home, 32);
    g_assert_true(g_str_has_suffix(path, "parent/places/32/user-home.png"));
    g_assert_null(lxp_icon_lookup("child", missing, 32));
    g_assert_null(lxp_icon_lookup("no-such-theme", missing, 32));
}

static LxpTheme *make_theme(void)
{
    LxpTheme *theme = lxp_theme_new("Preview");
    const char *font[] = { "Noto Sans", "12", "Bold", "Normal", NULL };

    write_file(g_get_user_data_dir(), "themes/Boxy/openbox-3/themerc",
               "window.active.title.bg: Raised Gradient Vertical\n"
               "window.active.title.bg.color: #ff0000\n"
               "window.active.title.bg.colorTo: #800000\n"
               "window.inactive.title.bg.color: #00ff00\n"
               "window.*.label.bg: Parentrelative\n"
               "window.*.label.text.color: #ffffff\n"
               "border.width: 2\n"
               "border.color: #0000ff\n"
               "padding.width: 4\n"
               "window.handle.width: 0\n"
               "window.label.text.justify: Center\n");

    lxp_theme_set_string(theme, "LXQt Palette", "window_color", "#202020");
    lxp_theme_set_string(theme, "LXQt Palette", "highlight_color", "#3584e4");
    lxp_theme_set_string(theme, "LXQt", "font", "Cantarell,11,-1,5,50,0,0,0,0,0");
    lxp_theme_set_string(theme, "Openbox", "theme", "Boxy");
    lxp_theme_set_string_list(theme, "Openbox", "font_ActiveWindow", font);
    lxp_theme_set_string(theme, "Panel", "position", "Top");
    lxp_theme_set_string(theme, "Panel", "panelSize", "40");
    lxp_theme_set_string(theme, "Panel", "opacity", "50");
    lxp_theme_set_string(theme, "Panel", "background-color",
                         "@Variant(\\0\\0\\0\\x43\\x1\\xff\\xff\\0\\0\\xff\\xff\\0\\0\\0\\0)");
    return theme;
}

static void test_model(void)
{
    g_autoptr(LxpTheme) theme = make_theme();
    g_autoptr(LxpPreviewModel) m = lxp_preview_model_new(theme);
    g_autoptr(LxpTheme) empty = lxp_theme_new("Empty");
    g_autoptr(LxpPreviewModel) d = lxp_preview_model_new(empty);
    g_autofree char *title = lxp_color_to_hex(&m->active.title_bg);
    g_autofree char *title_to = lxp_color_to_hex(&m->active.title_bg_to);
    g_autofree char *border = lxp_color_to_hex(&m->active.border);
    g_autofree char *panel = lxp_color_to_hex(&m->panel_bg);
    g_autofree char *menu_bg = lxp_color_to_hex(&m->menu_bg);

    g_assert_true(m->has_openbox);
    g_assert_cmpstr(title, ==, "#ff0000");
    g_assert_cmpstr(title_to, ==, "#800000");
    g_assert_true(m->active.title_gradient);
    g_assert_false(m->inactive.title_gradient);
    g_assert_false(m->active.label_bg_visible);
    g_assert_cmpstr(border, ==, "#0000ff");
    g_assert_cmpint(m->border_width, ==, 2);
    g_assert_cmpint(m->padding_height, ==, 4);  /* defaults to padding.width */
    g_assert_cmpint(m->handle_width, ==, 0);
    g_assert_cmpstr(m->title_justify, ==, "center");
    g_assert_cmpstr(m->title_font_family, ==, "Noto Sans");
    g_assert_true(m->title_font_bold);
    g_assert_cmpstr(m->font_family, ==, "Cantarell");
    g_assert_cmpfloat(m->font_size, ==, 11);
    /* no menu colors in the themerc: the Qt palette is used */
    g_assert_cmpstr(menu_bg, ==, "#202020");
    g_assert_cmpstr(panel, ==, "#00ff00");
    g_assert_cmpfloat(m->panel_opacity, ==, 0.5);
    g_assert_cmpint(m->panel_size, ==, 40);

    /* an empty theme still produces a complete model */
    g_assert_false(d->has_openbox);
    g_assert_cmpstr(d->panel_position, ==, "Bottom");
    g_assert_cmpstr(d->title_layout, ==, "NLIMC");
    g_assert_null(d->wallpaper);
}

static void test_panel_qss(void)
{
    g_autoptr(LxpTheme) theme = lxp_theme_new("Themed");
    g_autoptr(LxpPreviewModel) palette = NULL;
    g_autoptr(LxpPreviewModel) m = NULL;
    g_autoptr(LxpPreviewModel) colored = NULL;
    g_autofree char *hex = NULL;
    LxpPanelQss q;

    lxp_theme_set_string(theme, "LXQt Palette", "highlight_color", "#3584e4");
    palette = lxp_preview_model_new(theme);

    lxp_panel_qss_parse(
        "/* { color: red; } */\n"
        "LXQtPanel #BackgroundWidget {\n"
        "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #515048, stop:1 #3C3B37);\n"
        "}\n"
        "LXQtPanel[position=\"Top\"] #BackgroundWidget { background: #ff0000; }\n"
        "LXQtPanel[position=\"Bottom\"][layoutDirection=\"1\"] #BackgroundWidget { background: #00ff00; }\n"
        "LXQtPanel QSpinBox { color: #000000; background: #ffffff; }\n"
        "#TaskBar QToolButton { color: #f5f5f5; border-bottom: 2px solid #454545; background: transparent; }\n"
        "#TaskBar QToolButton:hover, #TaskBar QToolButton:on { background: rgba(0, 0, 0, 45%); }\n"
        "#TaskBar QToolButton:on { border-bottom: 2px solid palette(highlight); }\n"
        "#TaskBar QToolButton[urgent=\"true\"] { background: #0000ff; }\n",
        NULL, "Bottom", palette, &q);
    g_assert_true(q.has_bg);
    hex = lxp_color_to_hex(&q.bg);
    g_assert_cmpstr(hex, ==, "#515048");
    g_clear_pointer(&hex, g_free);
    hex = lxp_color_to_hex(&q.bg_to);
    g_assert_cmpstr(hex, ==, "#3c3b37");
    g_clear_pointer(&hex, g_free);
    g_assert_false(q.has_text);
    g_assert_false(q.task.has_bg);  /* transparent */
    g_assert_true(q.task.has_text && q.task_active.has_text);
    g_assert_cmpint(q.task.edge_side, ==, 'b');
    g_assert_true(q.task_active.has_bg);
    g_assert_cmpfloat_with_epsilon(q.task_active.bg.a, 0.45, 0.001);
    hex = lxp_color_to_hex(&q.task_active.edge);
    g_assert_cmpstr(hex, ==, "#3584e4");
    g_clear_pointer(&hex, g_free);

    /* rules for the panel's own position apply */
    lxp_panel_qss_parse("LXQtPanel #BackgroundWidget { background: rgba(40, 40, 40, 60%); }\n"
                        "LXQtPanel[position=\"Top\"] #BackgroundWidget { background: #ff0000; }",
                        NULL, "Top", palette, &q);
    hex = lxp_color_to_hex(&q.bg);
    g_assert_cmpstr(hex, ==, "#ff0000");
    g_clear_pointer(&hex, g_free);

    /* a theme naming an installed LXQt theme is previewed with its panel */
    write_file(g_get_user_data_dir(), "lxqt/themes/Smoky/lxqt-panel.qss",
               "LXQtPanel #BackgroundWidget { background: rgba(40, 40, 40, 60%); }\n"
               "#TaskBar QToolButton:on { background: #b95832; }\n");
    lxp_theme_set_string(theme, "LXQt", "theme", "Smoky");
    m = lxp_preview_model_new(theme);
    hex = lxp_color_to_hex(&m->panel_bg);
    g_assert_cmpstr(hex, ==, "#282828");
    g_clear_pointer(&hex, g_free);
    g_assert_cmpfloat_with_epsilon(m->panel_opacity, 0.6, 0.001);
    g_assert_cmpstr(m->panel_style, ==, "theme");
    g_assert_true(m->panel_task_active.has_bg);
    /* readable on the theme's dark panel, whatever the palette says */
    g_assert_cmpfloat(lxp_color_contrast(&m->panel_text, &m->panel_bg), >=, 3);

    /* a theme drawn with pictures: the middle of the picture is the fill */
    {
        g_autoptr(LxpPreviewModel) pictured = NULL;
        g_autofree char *fill = NULL;

        write_file(g_get_user_data_dir(), "lxqt/themes/Pictured/panel.svg",
                   "<svg xmlns='http://www.w3.org/2000/svg' width='11' height='11'>"
                   "<rect width='11' height='11' fill='#00ffff'/>"
                   "<rect x='2' y='2' width='7' height='7' fill='#100e23'/></svg>");
        write_file(g_get_user_data_dir(), "lxqt/themes/Pictured/lxqt-panel.qss",
                   "LXQtPanel[position=\"Bottom\"] #BackgroundWidget {\n"
                   "  border-image: url(panel.svg) 3 3 3 3 stretch stretch; }\n"
                   "LXQtPanel[position=\"Bottom\"] Plugin > QToolButton { color: #ffffff; }\n"
                   "LXQtPanel[position=\"Bottom\"] #TaskBar > QToolButton:on {\n"
                   "  border-image: url(../../../../escape.svg) 1; }\n");
        lxp_theme_set_string(theme, "LXQt", "theme", "Pictured");
        pictured = lxp_preview_model_new(theme);
        fill = lxp_color_to_hex(&pictured->panel_bg);
        g_assert_cmpstr(fill, ==, "#100e23");
        g_assert_false(pictured->panel_gradient);
        g_clear_pointer(&fill, g_free);
        fill = lxp_color_to_hex(&pictured->panel_text);
        g_assert_cmpstr(fill, ==, "#ffffff");
        /* files outside the theme are not read */
        g_assert_false(pictured->panel_task_active.has_bg);
    }

    /* a panel color of the user's replaces the theme's background */
    lxp_theme_set_string(theme, "Panel", "background-color", "#00ff00");
    lxp_theme_set_string(theme, "Panel", "opacity", "50");
    colored = lxp_preview_model_new(theme);
    hex = lxp_color_to_hex(&colored->panel_bg);
    g_assert_cmpstr(hex, ==, "#00ff00");
    g_assert_cmpfloat(colored->panel_opacity, ==, 0.5);
    g_assert_cmpstr(colored->panel_style, ==, "theme");
}

static guint32 pixel(cairo_surface_t *s, int x, int y)
{
    const guint8 *data = cairo_image_surface_get_data(s);
    int stride = cairo_image_surface_get_stride(s);

    return ((const guint32 *)(data + y * stride))[x] & 0xffffff;
}

static void test_render(void)
{
    g_autoptr(LxpTheme) theme = make_theme();
    g_autoptr(LxpPreviewModel) m = lxp_preview_model_new(theme);
    g_autofree char *png = g_build_filename(g_get_user_data_dir(), "out.png", NULL);
    g_autoptr(GError) error = NULL;
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                    LXP_PREVIEW_WIDTH, LXP_PREVIEW_HEIGHT);
    cairo_t *cr = cairo_create(s);

    lxp_preview_render(cr, m, LXP_PREVIEW_WIDTH, LXP_PREVIEW_HEIGHT);
    cairo_destroy(cr);
    cairo_surface_flush(s);

    /*
     * Panel on top (40 px) puts the active window's frame at (150, 360):
     * 2 px blue border, then the title gradient starting at red.
     */
    g_assert_cmphex(pixel(s, 150, 380), ==, 0x0000ff);
    g_assert_cmphex(pixel(s, 151, 380), ==, 0x0000ff);
    {
        guint32 top = pixel(s, 300, 363);
        guint32 bottom = pixel(s, 300, 362 + 28);

        g_assert_cmphex(top & 0x00ffff, ==, 0);
        g_assert_cmpuint(top >> 16, >, 0xe0);
        /* darker towards #800000 at the bottom */
        g_assert_cmpuint(bottom >> 16, <, 0x90);
    }
    /* panel: #00ff00 at 50% over the wallpaper-less dark background */
    g_assert_cmpuint((pixel(s, 700, 20) >> 8) & 0xff, >, 0x70);
    cairo_surface_destroy(s);

    g_assert_true(lxp_preview_render_png(m, 320, 200, png, &error));
    g_assert_no_error(error);
    g_assert_true(g_file_test(png, G_FILE_TEST_IS_REGULAR));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func("/preview/icons", test_icons);
    g_test_add_func("/preview/model", test_model);
    g_test_add_func("/preview/render", test_render);
    g_test_add_func("/preview/panel-qss", test_panel_qss);
    return g_test_run();
}
