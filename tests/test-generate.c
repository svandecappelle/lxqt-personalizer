/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-apply.h"
#include "lxp-assets.h"
#include "lxp-error.h"
#include "lxp-preview-model.h"
#include "lxp-recolor.h"
#include "lxp-scheme.h"
#include "lxp-template.h"
#include "test-util.h"

#include <string.h>

static gboolean exists_in(const char *dir, const char *rel)
{
    g_autofree char *path = g_build_filename(dir, rel, NULL);
    return g_file_test(path, G_FILE_TEST_EXISTS);
}

static LxpColor C(const char *text)
{
    LxpColor c;
    g_assert_true(lxp_color_parse(text, &c));
    return c;
}

static void assert_hex(const LxpColor *c, const char *expected)
{
    g_autofree char *hex = lxp_color_to_hex(c);
    g_assert_cmpstr(hex, ==, expected);
}

static void test_template(void)
{
    g_autoptr(GHashTable) vars = g_hash_table_new(g_str_hash, g_str_equal);
    g_autoptr(GError) error = NULL;
    g_autofree char *out = NULL;
    char *bad;

    g_hash_table_insert(vars, "accent", "#88c0d0");
    out = lxp_template_expand("a: {{accent}}; b: {{ accent }}", vars, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(out, ==, "a: #88c0d0; b: #88c0d0");

    bad = lxp_template_expand("{{nope}}", vars, &error);
    g_assert_null(bad);
    g_assert_error(error, LXP_ERROR, LXP_ERROR_INVALID);
    g_clear_error(&error);
    bad = lxp_template_expand("{{accent", vars, &error);
    g_assert_null(bad);
    g_assert_error(error, LXP_ERROR, LXP_ERROR_INVALID);
}

/* Adwaita dark -> Nord */
static const LxpRecolorPalette adwaita = {
    { 0x35 / 255.0, 0x35 / 255.0, 0x35 / 255.0, 1 },  /* background #353535 */
    { 0x2d / 255.0, 0x2d / 255.0, 0x2d / 255.0, 1 },  /* surface #2d2d2d */
    { 0xee / 255.0, 0xee / 255.0, 0xec / 255.0, 1 },  /* foreground #eeeeec */
    { 0x15 / 255.0, 0x53 / 255.0, 0x9e / 255.0, 1 },  /* accent #15539e */
};

static LxpRecolorPalette nord(void)
{
    LxpRecolorPalette p = { C("#2e3440"), C("#3b4252"), C("#eceff4"), C("#88c0d0") };
    return p;
}

static void test_recolor_color(void)
{
    LxpRecolorPalette to = nord();
    LxpColor out;
    LxpColor border = C("#1b1b1b"), red = C("#e01b24"), dim = C("#919190");
    LxpColor translucent = C("#ffffff");

    out = lxp_recolor_color(&adwaita.background, &adwaita, &to);
    assert_hex(&out, "#2e3440");
    out = lxp_recolor_color(&adwaita.foreground, &adwaita, &to);
    assert_hex(&out, "#eceff4");
    out = lxp_recolor_color(&adwaita.surface, &adwaita, &to);
    assert_hex(&out, "#3b4252");
    out = lxp_recolor_color(&adwaita.accent, &adwaita, &to);
    assert_hex(&out, "#88c0d0");

    /* darker than the background stays darker, even though Nord's surface is lighter */
    out = lxp_recolor_color(&border, &adwaita, &to);
    g_assert_cmpfloat(lxp_color_luminance(&out), <, lxp_color_luminance(&to.background));
    /* halfway between background and text stays halfway */
    out = lxp_recolor_color(&dim, &adwaita, &to);
    g_assert_cmpfloat(lxp_color_luminance(&out), >, lxp_color_luminance(&to.background));
    g_assert_cmpfloat(lxp_color_luminance(&out), <, lxp_color_luminance(&to.foreground));
    /* status colors are kept */
    out = lxp_recolor_color(&red, &adwaita, &to);
    assert_hex(&out, "#e01b24");
    /* alpha is kept */
    translucent.a = 0.3;
    out = lxp_recolor_color(&translucent, &adwaita, &to);
    g_assert_cmpfloat(out.a, ==, 0.3);
}

static void test_recolor_css(void)
{
    LxpRecolorPalette to = nord();
    g_autofree char *css = lxp_recolor_css(
        "@define-color theme_bg_color #353535;\n"
        "/* #353535 in a comment stays */\n"
        "#TaskBar, .a { color: #eeeeec; background: rgba(21, 83, 158, 0.5); }\n"
        "b { border-color: white; outline-color: whitesmoke; }\n"
        "c { color: #15539e; }\n",
        &adwaita, &to);
    LxpRecolorPalette parsed;

    g_assert_nonnull(strstr(css, "@define-color theme_bg_color #2e3440;"));
    g_assert_nonnull(strstr(css, "/* #353535 in a comment stays */"));
    g_assert_nonnull(strstr(css, "#TaskBar, .a { color: #eceff4; background: rgba(136, 192, 208, 0.5); }"));
    g_assert_nonnull(strstr(css, "outline-color: whitesmoke"));
    g_assert_null(strstr(css, "border-color: white;"));
    g_assert_nonnull(strstr(css, "c { color: #88c0d0; }"));

    g_assert_true(lxp_recolor_palette_from_gtk_css(
        "@define-color theme_fg_color #eeeeec;\n@define-color theme_bg_color #353535;\n"
        "@define-color theme_base_color #2d2d2d;\n@define-color theme_selected_bg_color #15539e;\n",
        &parsed));
    assert_hex(&parsed.accent, "#15539e");
    g_assert_false(lxp_recolor_palette_from_gtk_css("a { }", &parsed));
}

static LxpTheme *nord_theme(const char *name)
{
    LxpTheme *theme = lxp_theme_new(name);
    LxpScheme s = { 0 };

    s.background = C("#2e3440");
    s.surface = C("#3b4252");
    s.foreground = C("#eceff4");
    s.accent = C("#88c0d0");
    s.font = g_strdup("Noto Sans 11");
    lxp_scheme_save(&s, theme);
    lxp_scheme_clear(&s);
    return theme;
}

static void test_derive(void)
{
    g_autoptr(LxpTheme) theme = nord_theme("Nord");
    g_autoptr(GError) error = NULL;
    LxpScheme s;
    LxpSchemeColors c;

    g_assert_true(lxp_scheme_load(theme, &s, &error));
    lxp_scheme_derive(&s, &c);
    g_assert_true(c.dark);
    /* light accent gets dark text, readable */
    g_assert_cmpfloat(lxp_color_contrast(&c.accent, &c.accent_fg), >=, 4.5);
    g_assert_cmpfloat(lxp_color_luminance(&c.accent_fg), <, 0.1);
    g_assert_cmpfloat(lxp_color_contrast(&c.panel, &c.panel_fg), >=, 4.5);
    g_assert_cmpfloat(lxp_color_luminance(&c.panel), <, lxp_color_luminance(&c.bg));
    /* no title color: neutral title bars */
    assert_hex(&c.title_inactive, "#2e3440");
    lxp_scheme_clear(&s);

    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "accent", "bogus");
    g_assert_false(lxp_scheme_load(theme, &s, &error));
    g_assert_error(error, LXP_ERROR, LXP_ERROR_INVALID);
}

static void test_generate(void)
{
    g_autoptr(LxpTheme) theme = nord_theme("Nord Night");
    g_autofree char *dir = g_build_filename(g_get_user_data_dir(), "nord", NULL);
    g_autoptr(GError) error = NULL;
    static const char *const files[] = {
        "data/themes/lxp-nord-night/.lxp-asset",
        "data/themes/lxp-nord-night/openbox-3/themerc",
        "data/themes/lxp-nord-night/gtk-3.0/gtk.css",
        "data/themes/lxp-nord-night/gtk-4.0/gtk.css",
        "data/lxqt/themes/lxp-nord-night/.lxp-asset",
        "data/lxqt/themes/lxp-nord-night/lxqt-panel.qss",
        "data/lxqt/themes/lxp-nord-night/lxqt-runner.qss",
        "data/lxqt/themes/lxp-nord-night/lxqt-notificationd.qss",
    };
    g_autofree char *themerc = NULL;
    g_autofree char *qss = NULL;
    g_autofree char *value = NULL;
    g_autofree char *stale = NULL;

    write_file(dir, "data/themes/lxp-nord-night/stale-file", "old");
    g_assert_true(lxp_scheme_generate(theme, dir, &error));
    g_assert_no_error(error);

    for (gsize i = 0; i < G_N_ELEMENTS(files); i++) {
        g_autofree char *path = g_build_filename(dir, files[i], NULL);
        if (!g_file_test(path, G_FILE_TEST_IS_REGULAR))
            g_error("missing %s", files[i]);
    }
    stale = g_build_filename(dir, "data/themes/lxp-nord-night/stale-file", NULL);
    g_assert_false(g_file_test(stale, G_FILE_TEST_EXISTS));

    themerc = read_file(dir, "data/themes/lxp-nord-night/openbox-3/themerc");
    g_assert_nonnull(strstr(themerc, "menu.items.active.bg.color: #88c0d0"));
    g_assert_null(strstr(themerc, "{{"));
    qss = read_file(dir, "data/lxqt/themes/lxp-nord-night/lxqt-panel.qss");
    g_assert_nonnull(strstr(qss, "rgba(136, 192, 208, 0.40)"));

    value = lxp_theme_get_string(theme, "LXQt", "theme");
    g_assert_cmpstr(value, ==, "lxp-nord-night");
    g_free(value);
    value = lxp_theme_get_string(theme, "LXQt Palette", "highlight_color");
    g_assert_cmpstr(value, ==, "#88c0d0");
    g_free(value);
    value = lxp_theme_get_string(theme, "LXQt", "font");
    g_assert_cmpstr(value, ==, "Noto Sans,11,-1,5,50,0,0,0,0,0");
    g_free(value);
    value = lxp_theme_get_string(theme, "GTK", "prefer_dark");
    g_assert_cmpstr(value, ==, "true");
}

static void test_assets(void)
{
    g_autoptr(LxpTheme) theme = nord_theme("Nord Night");
    g_autofree char *dir = g_build_filename(g_get_user_config_dir(), "src-theme", NULL);
    g_autofree char *installed = NULL;
    g_autofree char *foreign = NULL;
    g_autofree char *found = NULL;
    g_autoptr(GError) error = NULL;

    g_assert_true(lxp_scheme_generate(theme, dir, &error));
    g_assert_true(lxp_theme_save(theme, dir, &error));

    found = lxp_assets_find(theme, "themes", "lxp-nord-night", "openbox-3/themerc");
    g_assert_nonnull(found);
    g_assert_null(lxp_assets_find(theme, "themes", "../x", NULL));

    /* first install, then a reinstall replaces the copy */
    g_assert_true(lxp_assets_install(theme, &error));
    g_assert_no_error(error);
    installed = read_file(g_get_user_data_dir(), "themes/lxp-nord-night/openbox-3/themerc");
    g_assert_nonnull(installed);
    write_file(g_get_user_data_dir(), "themes/lxp-nord-night/extra", "x");
    g_assert_true(lxp_assets_install(theme, &error));
    g_assert_null(read_file(g_get_user_data_dir(), "themes/lxp-nord-night/extra"));

    /* never overwrite a directory the user installed some other way */
    {
        g_autofree char *lxqt = g_build_filename(g_get_user_data_dir(), "lxqt", NULL);
        lxp_remove_tree(lxqt, NULL);
    }
    write_file(g_get_user_data_dir(), "lxqt/themes/lxp-nord-night/lxqt-panel.qss", "mine");
    g_assert_false(lxp_assets_install(theme, &error));
    g_assert_error(error, LXP_ERROR, LXP_ERROR_EXISTS);
    foreign = read_file(g_get_user_data_dir(), "lxqt/themes/lxp-nord-night/lxqt-panel.qss");
    g_assert_cmpstr(foreign, ==, "mine");
}

static void test_apply_generated(void)
{
    g_autoptr(LxpTheme) theme = nord_theme("Nord Night");
    g_autofree char *dir = g_build_filename(g_get_user_config_dir(), "src-theme", NULL);
    g_autoptr(LxpTheme) loaded = NULL;
    g_autoptr(LxpPreviewModel) model = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree char *id = NULL;
    g_autofree char *conf = NULL;
    g_autofree char *title = NULL;
    const char *only[] = { "lxqt", NULL };

    install_fixture("config/lxqt/lxqt.conf", g_get_user_config_dir(), "lxqt/lxqt.conf");
    g_assert_true(lxp_scheme_generate(theme, dir, &error));
    g_assert_true(lxp_theme_save(theme, dir, &error));
    loaded = lxp_theme_load(dir, &error);
    g_assert_no_error(error);

    /* the preview uses the bundled Openbox theme before installation */
    model = lxp_preview_model_new(loaded);
    g_assert_true(model->has_openbox);

    /* the generated LXQt theme is installed, so the check passes */
    id = lxp_apply_theme(loaded, only, LXP_APPLY_DEFAULT, &error);
    g_assert_no_error(error);
    conf = read_file(g_get_user_config_dir(), "lxqt/lxqt.conf");
    g_assert_nonnull(strstr(conf, "theme=lxp-nord-night\n"));
    g_assert_nonnull(strstr(conf, "highlight_color=#88c0d0\n"));
    title = read_file(g_get_user_data_dir(), "lxqt/themes/lxp-nord-night/lxqt-panel.qss");
    g_assert_nonnull(title);
}

static void test_rename_and_wallpaper(void)
{
    g_autoptr(LxpTheme) theme = nord_theme("Nord Night");
    g_autofree char *dir = g_build_filename(g_get_user_data_dir(), "nord", NULL);
    g_autofree char *outside = g_build_filename(g_get_user_config_dir(), "bg.png", NULL);
    g_autofree char *old_unit = g_build_filename(dir, "data/themes/lxp-nord-night", NULL);
    g_autofree char *new_unit = g_build_filename(dir, "data/themes/lxp-polar", NULL);
    g_autofree char *bundled = NULL;
    g_autofree char *inside = NULL;
    g_autofree char *value = NULL;
    g_autoptr(GError) error = NULL;

    g_assert_true(lxp_scheme_generate(theme, dir, &error));
    g_assert_true(g_file_test(old_unit, G_FILE_TEST_IS_DIR));

    /* a renamed theme leaves no stale generated themes behind */
    lxp_theme_set_string(theme, LXP_THEME_GROUP, "Name", "Polar");
    g_assert_true(lxp_scheme_generate(theme, dir, &error));
    g_assert_no_error(error);
    g_assert_false(g_file_test(old_unit, G_FILE_TEST_EXISTS));
    g_assert_true(g_file_test(new_unit, G_FILE_TEST_IS_DIR));

    /* a wallpaper outside the theme is copied in and made relative */
    write_file(g_get_user_config_dir(), "bg.png", "PNG");
    lxp_theme_set_string(theme, "Wallpaper", "file", outside);
    g_assert_true(lxp_theme_bundle_wallpaper(theme, dir, &error));
    value = lxp_theme_get_string(theme, "Wallpaper", "file");
    g_assert_cmpstr(value, ==, "wallpaper.png");
    bundled = read_file(dir, "wallpaper.png");
    g_assert_cmpstr(bundled, ==, "PNG");

    /* an absolute path inside the theme becomes relative again */
    g_free(value);
    inside = g_build_filename(dir, "wallpaper.png", NULL);
    lxp_theme_set_string(theme, "Wallpaper", "file", inside);
    g_assert_true(lxp_theme_bundle_wallpaper(theme, dir, &error));
    value = lxp_theme_get_string(theme, "Wallpaper", "file");
    g_assert_cmpstr(value, ==, "wallpaper.png");
}

static void test_keep_installed(void)
{
    g_autoptr(LxpTheme) theme = nord_theme("Mixed");
    g_autofree char *dir = g_build_filename(g_get_user_data_dir(), "mixed", NULL);
    g_autoptr(GError) error = NULL;
    g_autofree char *openbox = NULL, *gtk = NULL, *style = NULL, *generate = NULL;
    LxpScheme s;

    /* keep an installed window theme and widget style, generate the rest */
    g_assert_true(lxp_scheme_load(theme, &s, &error));
    s.keep_openbox = TRUE;
    s.style = g_strdup("kvantum");
    lxp_scheme_save(&s, theme);
    lxp_scheme_clear(&s);
    lxp_theme_set_string(theme, "Openbox", "theme", "Arrondie-Violet");

    g_assert_true(lxp_scheme_generate(theme, dir, &error));
    g_assert_no_error(error);
    openbox = lxp_theme_get_string(theme, "Openbox", "theme");
    gtk = lxp_theme_get_string(theme, "GTK", "theme");
    style = lxp_theme_get_string(theme, "LXQt", "style");
    generate = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "generate");
    g_assert_cmpstr(openbox, ==, "Arrondie-Violet");
    g_assert_cmpstr(gtk, ==, "lxp-mixed");
    g_assert_cmpstr(style, ==, "kvantum");
    g_assert_cmpstr(generate, ==, "gtk;lxqt;kvantum;terminal;");
    g_assert_false(exists_in(dir, "data/themes/lxp-mixed/openbox-3/themerc"));
    g_assert_true(exists_in(dir, "data/themes/lxp-mixed/gtk-3.0/gtk.css"));

    /* the choice survives a reload */
    g_assert_true(lxp_scheme_load(theme, &s, &error));
    g_assert_true(s.keep_openbox);
    g_assert_false(s.keep_gtk);
    lxp_scheme_clear(&s);
}

static void test_scheme_options(void)
{
    g_autoptr(LxpTheme) theme = nord_theme("Options");
    g_autofree char *dir = g_build_filename(g_get_user_data_dir(), "options", NULL);
    g_autoptr(GError) error = NULL;
    g_auto(GStrv) title = NULL, item = NULL;
    g_autofree char *gtk_font = NULL, *dark = NULL, *link = NULL, *follow = NULL;

    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "title_font", "Noto Serif Bold Italic 12");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "gtk_font", "Cantarell 11");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "prefer_dark", "false");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "link", "#ff0000");
    lxp_theme_set_string(theme, "LXQt", "icon_follow_color_scheme", "false");

    g_assert_true(lxp_scheme_generate(theme, dir, &error));
    g_assert_no_error(error);

    title = lxp_theme_get_string_list(theme, "Openbox", "font_ActiveWindow");
    g_assert_cmpstr(title[0], ==, "Noto Serif");
    g_assert_cmpstr(title[1], ==, "12");
    g_assert_cmpstr(title[2], ==, "Bold");
    g_assert_cmpstr(title[3], ==, "Italic");
    item = lxp_theme_get_string_list(theme, "Openbox", "font_MenuItem");
    g_assert_cmpstr(item[0], ==, "Noto Sans");   /* the text font */
    g_assert_cmpstr(item[2], ==, "Normal");

    gtk_font = lxp_theme_get_string(theme, "GTK", "font");
    g_assert_cmpstr(gtk_font, ==, "Cantarell 11");
    dark = lxp_theme_get_string(theme, "GTK", "prefer_dark");
    g_assert_cmpstr(dark, ==, "false");          /* overrides the dark colors */
    link = lxp_theme_get_string(theme, "LXQt Palette", "link_color");
    g_assert_cmpstr(link, ==, "#ff0000");
    follow = lxp_theme_get_string(theme, "LXQt", "icon_follow_color_scheme");
    g_assert_cmpstr(follow, ==, "false");        /* no longer forced */
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func("/generate/template", test_template);
    g_test_add_func("/generate/recolor-color", test_recolor_color);
    g_test_add_func("/generate/recolor-css", test_recolor_css);
    g_test_add_func("/generate/derive", test_derive);
    g_test_add_func("/generate/generate", test_generate);
    g_test_add_func("/generate/assets", test_assets);
    g_test_add_func("/generate/apply", test_apply_generated);
    g_test_add_func("/generate/rename-and-wallpaper", test_rename_and_wallpaper);
    g_test_add_func("/generate/keep-installed", test_keep_installed);
    g_test_add_func("/generate/scheme-options", test_scheme_options);
    return g_test_run();
}
