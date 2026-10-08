/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-assets.h"
#include "lxp-error.h"
#include "lxp-libconfig.h"
#include "lxp-palette.h"
#include "lxp-paths.h"
#include "lxp-preview-model.h"
#include "lxp-scheme.h"
#include "lxp-themegen.h"
#include "lxp-wallgen.h"
#include "lxp-xbm.h"
#include "test-util.h"

#include <math.h>
#include <string.h>

static gboolean readable(const char *dir, const char *rel)
{
    g_autofree char *data = read_file(dir, rel);
    return data != NULL;
}

static LxpColor C(const char *text)
{
    LxpColor c;
    g_assert_true(lxp_color_parse(text, &c));
    return c;
}

static LxpTheme *scheme_theme(const char *name)
{
    LxpTheme *theme = lxp_theme_new(name);

    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "background", "#ececec");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "surface", "#ffffff");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "foreground", "#1d1d1f");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "accent", "#0a84ff");
    return theme;
}

static void test_xbm(void)
{
    g_autoptr(LxpXbm) dot = lxp_xbm_new(12, 12);
    g_autoptr(LxpXbm) parsed = NULL;
    g_autofree char *text = NULL;

    lxp_xbm_fill_circle(dot, 6, 6, 6);
    g_assert_true(lxp_xbm_get(dot, 6, 6));
    g_assert_false(lxp_xbm_get(dot, 0, 0));
    text = lxp_xbm_to_text(dot, "close");
    g_assert_nonnull(strstr(text, "#define close_width 12"));

    parsed = lxp_xbm_parse(text);
    g_assert_nonnull(parsed);
    g_assert_cmpint(parsed->width, ==, 12);
    g_assert_cmpmem(parsed->bits, 24, dot->bits, 24);
    g_assert_null(lxp_xbm_parse("#define x_width 999\n#define x_height 1\n{0}"));
    g_assert_null(lxp_xbm_parse("garbage"));
}

static void test_window_style(void)
{
    g_autoptr(LxpTheme) theme = scheme_theme("Dots");
    g_autofree char *dir = g_build_filename(g_get_user_data_dir(), "dots", NULL);
    g_autoptr(GError) error = NULL;
    g_autofree char *themerc = NULL;
    g_autoptr(LxpTheme) loaded = NULL;
    g_autoptr(LxpPreviewModel) m = NULL;
    g_autofree char *close_fg = NULL;

    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "buttons", "dots");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "title_style", "gradient");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "title_padding", "7");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "border_width", "0");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "title_align", "left");
    g_assert_true(lxp_scheme_generate(theme, dir, &error));
    g_assert_true(lxp_theme_save(theme, dir, &error));
    g_assert_no_error(error);

    themerc = read_file(dir, "data/themes/lxp-dots/openbox-3/themerc");
    g_assert_nonnull(strstr(themerc, "border.width: 0\n"));
    g_assert_nonnull(strstr(themerc, "padding.height: 7\n"));
    g_assert_nonnull(strstr(themerc, "window.active.title.bg: Flat Gradient Vertical\n"));
    g_assert_nonnull(strstr(themerc, "window.label.text.justify: left\n"));
    g_assert_nonnull(strstr(themerc, "window.active.button.close.unpressed.image.color: #ff5f57\n"));
    g_assert_null(strstr(themerc, "{{"));
    g_assert_true(readable(dir, "data/themes/lxp-dots/openbox-3/close.xbm"));

    /* the preview draws the bitmaps in their own colors */
    loaded = lxp_theme_load(dir, &error);
    m = lxp_preview_model_new(loaded);
    g_assert_nonnull(m->button_bitmaps[LXP_BUTTON_CLOSE]);
    close_fg = lxp_color_to_hex(&m->active.button_kind_fg[LXP_BUTTON_CLOSE]);
    g_assert_cmpstr(close_fg, ==, "#ff5f57");
    g_assert_true(m->active.title_gradient);
    g_assert_cmpint(m->border_width, ==, 0);
}

static void test_wallpaper(void)
{
    g_autoptr(LxpTheme) theme = scheme_theme("Walls");
    g_autofree char *dir = g_build_filename(g_get_user_data_dir(), "walls", NULL);
    g_autofree char *png = g_build_filename(dir, LXP_GENERATED_WALLPAPER, NULL);
    g_autoptr(GError) error = NULL;
    g_autofree char *file = NULL;
    GStatBuf before, after;

    g_assert_true(lxp_wallgen_is_style("waves"));
    g_assert_false(lxp_wallgen_is_style("plaid"));

    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "wallpaper", "waves");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "wallpaper_seed", "4");
    g_assert_true(lxp_theme_generate(theme, dir, 320, 180, &error));
    g_assert_no_error(error);
    file = lxp_theme_get_string(theme, "Wallpaper", "file");
    g_assert_cmpstr(file, ==, LXP_GENERATED_WALLPAPER);
    g_assert_cmpint(g_stat(png, &before), ==, 0);

    /* unchanged inputs: not rendered again */
    g_usleep(1100 * 1000);
    g_assert_true(lxp_theme_generate(theme, dir, 320, 180, &error));
    g_assert_cmpint(g_stat(png, &after), ==, 0);
    g_assert_cmpint(before.st_mtime, ==, after.st_mtime);

    /* switching to an image file drops the generated one */
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "wallpaper", NULL);
    lxp_theme_set_string(theme, "Wallpaper", "file", "/elsewhere.png");
    g_assert_true(lxp_theme_generate(theme, dir, 320, 180, &error));
    g_assert_false(g_file_test(png, G_FILE_TEST_EXISTS));
}

static void test_palette(void)
{
    LxpScheme s = { 0 };
    LxpSchemeColors c;
    g_autofree char *png = g_build_filename(g_get_user_data_dir(), "red.png", NULL);
    LxpColor red = C("#e01b24"), green;
    double h, sat, l;

    /* an image that is mostly red */
    {
        cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 64, 64);
        cairo_t *cr = cairo_create(surface);
        g_mkdir_with_parents(g_get_user_data_dir(), 0755);
        cairo_set_source_rgb(cr, red.r, red.g, red.b);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_write_to_png(surface, png);
        cairo_surface_destroy(surface);
    }
    g_assert_true(lxp_palette_from_image(png, TRUE, &s));
    lxp_scheme_derive(&s, &c);
    g_assert_true(c.dark);
    lxp_color_to_hsl(&s.accent, &h, &sat, &l);
    g_assert_true(h < 15 || h > 345);    /* a red accent */
    g_assert_cmpfloat(lxp_color_contrast(&s.foreground, &s.background), >, 7);

    lxp_palette_flip(&s);
    lxp_scheme_derive(&s, &c);
    g_assert_false(c.dark);

    green = lxp_palette_harmony(&red, LXP_HARMONY_COMPLEMENTARY);
    lxp_color_to_hsl(&green, &h, &sat, &l);
    g_assert_cmpfloat(fabs(h - 177), <, 5);
    g_assert_false(lxp_palette_from_image("/no/such.png", TRUE, &s));
}

static void test_libconfig_structured(void)
{
    g_autoptr(LxpLibconfig) conf = lxp_libconfig_new_from_data(
        "animations = (\n  { preset = \"appear\"; },\n  { preset = \"disappear\"; }\n);\n"
        "shadow = true;\n");
    g_autofree char *anim = lxp_libconfig_get(conf, "animations");
    g_autofree char *shadow = lxp_libconfig_get(conf, "shadow");

    g_assert_true(g_str_has_prefix(anim, "("));
    g_assert_true(g_str_has_suffix(anim, ")"));
    g_assert_cmpstr(shadow, ==, "true");
    lxp_libconfig_set_raw(conf, "animations", "()");
    g_assert_cmpstr(lxp_libconfig_to_data(conf), ==, "animations = ();\nshadow = true;\n");
}

static void test_builtin(void)
{
    g_autofree char *root = g_build_filename(g_get_user_data_dir(), "shipped", NULL);
    g_autofree char *builtin = g_build_filename(root, "mac-like", NULL);
    g_autoptr(GPtrArray) list = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree char *resolved = NULL;
    g_autofree char *usable = NULL;
    g_autofree char *again = NULL;
    g_autofree char *user_themes = lxp_paths_themes_dir();

    write_file(builtin, "theme.ini",
               "[Theme]\nName=Mac-like\n\n[Scheme]\nbackground=#ececec\nsurface=#ffffff\n"
               "foreground=#1d1d1f\naccent=#0a84ff\nbuttons=dots\n");
    write_file(user_themes, "mine/theme.ini", "[Theme]\nName=Mine\n");
    g_setenv("LXP_BUILTIN_DIR", root, TRUE);
    /* $XDG_DATA_DIRS listing the user's data dir must not make it built in */
    g_setenv("XDG_DATA_DIRS", g_get_user_data_dir(), TRUE);

    list = lxp_theme_list_builtin();
    g_assert_cmpuint(list->len, ==, 1);
    g_assert_true(lxp_theme_is_builtin(builtin));
    {
        g_autofree char *mine = g_build_filename(user_themes, "mine", NULL);
        g_assert_false(lxp_theme_is_builtin(mine));
    }

    resolved = lxp_theme_resolve("Mac-like", &error);
    g_assert_no_error(error);
    usable = lxp_theme_materialize(resolved, &error);
    g_assert_no_error(error);
    g_assert_false(lxp_theme_is_builtin(usable));
    g_assert_true(readable(usable, "data/themes/lxp-mac-like/openbox-3/close.xbm"));
    g_assert_false(readable(builtin, "data/themes/lxp-mac-like/openbox-3/themerc"));

    /* reused until the built-in changes */
    again = lxp_theme_materialize(resolved, &error);
    g_assert_cmpstr(again, ==, usable);
    g_unsetenv("LXP_BUILTIN_DIR");
}

static void test_kvantum_generation(void)
{
    g_autoptr(LxpTheme) theme = scheme_theme("Rounded");
    g_autofree char *dir = g_build_filename(g_get_user_data_dir(), "rounded", NULL);
    g_autoptr(GError) error = NULL;
    g_autofree char *svg = NULL, *kvconfig = NULL, *name = NULL, *installed = NULL;
    static const char *const ids[] = {
        "button-normal-topleft", "button-focused", "lineedit-focused-bottomright",
        "menu-normal-top", "itemview-pressed", "scrollbarslider-normal",
        "checkbox-checked-normal", "radio-normal", "arrow-down-normal", "slidercursor-pressed",
    };

    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "style", "kvantum");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "widget_radius", "8");
    g_assert_true(lxp_scheme_generate(theme, dir, &error));
    g_assert_true(lxp_theme_save(theme, dir, &error));
    g_assert_no_error(error);

    name = lxp_theme_get_string(theme, "Kvantum", "theme");
    g_assert_cmpstr(name, ==, "lxp-rounded");
    svg = read_file(dir, "config/Kvantum/lxp-rounded/lxp-rounded.svg");
    kvconfig = read_file(dir, "config/Kvantum/lxp-rounded/lxp-rounded.kvconfig");
    g_assert_nonnull(svg);
    for (gsize i = 0; i < G_N_ELEMENTS(ids); i++) {
        g_autofree char *id = g_strdup_printf("id=\"%s\"", ids[i]);
        if (!strstr(svg, id))
            g_error("missing SVG element %s", ids[i]);
    }
    g_assert_nonnull(strstr(svg, "A 8,8 0 0 1"));            /* rounded corners */
    g_assert_nonnull(strstr(kvconfig, "highlight.color=#0a84ff"));
    g_assert_nonnull(strstr(kvconfig, "frame.top=8"));
    g_assert_nonnull(strstr(kvconfig, "[CheckBox]"));

    /* installed into $XDG_CONFIG_HOME/Kvantum on apply */
    {
        g_autoptr(LxpTheme) loaded = lxp_theme_load(dir, &error);
        g_assert_true(lxp_assets_install(loaded, &error));
        g_assert_no_error(error);
    }
    installed = read_file(g_get_user_config_dir(), "Kvantum/lxp-rounded/lxp-rounded.kvconfig");
    g_assert_cmpstr(installed, ==, kvconfig);

    /* Fusion: no Kvantum theme generated */
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "style", "Fusion");
    g_assert_true(lxp_scheme_generate(theme, dir, &error));
    g_assert_false(readable(dir, "config/Kvantum/lxp-rounded/lxp-rounded.svg"));
}

static void test_gtk_corners_and_panel_styles(void)
{
    g_autoptr(LxpTheme) theme = scheme_theme("Shapes");
    g_autofree char *dir = g_build_filename(g_get_user_data_dir(), "shapes", NULL);
    g_autoptr(GError) error = NULL;
    static const struct { const char *style, *expect; } styles[] = {
        { "pills", "border-radius: 10px" },
        { "flat", "margin: 0;" },
        { "underline", "border-bottom: 2px solid #0a84ff;" },
    };

    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "widget_radius", "9");
    for (gsize i = 0; i < G_N_ELEMENTS(styles); i++) {
        g_autofree char *qss = NULL;

        lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "panel_style", styles[i].style);
        g_assert_true(lxp_scheme_generate(theme, dir, &error));
        g_assert_no_error(error);
        qss = read_file(dir, "data/lxqt/themes/lxp-shapes/lxqt-panel.qss");
        if (!strstr(qss, styles[i].expect))
            g_error("panel style %s: missing “%s”", styles[i].style, styles[i].expect);
        g_assert_null(strstr(qss, "{{"));
    }

    /* the GTK themes get the widget corners, when GTK's own CSS was recolored */
    {
        g_autofree char *gtk3 = read_file(dir, "data/themes/lxp-shapes/gtk-3.0/gtk.css");
        g_autofree char *gtk4 = read_file(dir, "data/themes/lxp-shapes/gtk-4.0/gtk.css");

        if (strstr(gtk3, "from GTK 3's own stylesheet")) {
            g_assert_nonnull(strstr(gtk3, "button, entry, spinbutton { border-radius: 9px; }"));
            g_assert_nonnull(strstr(gtk3, "popover.background { border-radius: 11px; }"));
        }
        if (strstr(gtk4, "from GTK 4's own stylesheet"))
            g_assert_nonnull(strstr(gtk4, "popover > contents { border-radius: 11px; }"));
    }
}

static void test_config_kinds(void)
{
    g_autofree char *dir = g_build_filename(g_get_user_data_dir(), "sneaky", NULL);
    g_autoptr(LxpTheme) theme = lxp_theme_new("Sneaky");
    g_autoptr(GError) error = NULL;

    /* an autostart entry would run a program at login: never installed */
    write_file(dir, "config/autostart/.lxp-asset", "");
    write_file(dir, "config/autostart/evil.desktop", "[Desktop Entry]\nExec=/bin/true\n");
    g_assert_true(lxp_theme_save(theme, dir, &error));
    g_assert_false(lxp_assets_install(theme, &error));
    g_assert_error(error, LXP_ERROR, LXP_ERROR_INVALID);
    g_assert_false(readable(g_get_user_config_dir(), "autostart/evil.desktop"));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func("/tier1/xbm", test_xbm);
    g_test_add_func("/tier1/window-style", test_window_style);
    g_test_add_func("/tier1/wallpaper", test_wallpaper);
    g_test_add_func("/tier1/palette", test_palette);
    g_test_add_func("/tier1/libconfig-structured", test_libconfig_structured);
    g_test_add_func("/tier1/builtin", test_builtin);
    g_test_add_func("/tier2/kvantum-generation", test_kvantum_generation);
    g_test_add_func("/tier2/config-kinds", test_config_kinds);
    g_test_add_func("/tier2/gtk-corners-and-panel-styles", test_gtk_corners_and_panel_styles);
    return g_test_run();
}
