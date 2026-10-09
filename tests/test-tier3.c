/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-backend.h"
#include "lxp-error.h"
#include "lxp-iconcolor.h"
#include "lxp-scheme.h"
#include "lxp-surprise.h"
#include "lxp-terminal-gen.h"
#include "lxp-themegen.h"
#include "test-util.h"

#include <string.h>

static LxpTheme *scheme_theme(const char *name)
{
    LxpTheme *theme = lxp_theme_new(name);

    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "background", "#ececec");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "surface", "#ffffff");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "foreground", "#1d1d1f");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "accent", "#0a84ff");
    return theme;
}

static void test_terminal(void)
{
    const LxpBackend *terminal = lxp_backend_find("terminal");
    g_autoptr(LxpTheme) theme = scheme_theme("Term");
    g_autoptr(LxpTheme) bad = lxp_theme_new("Bad");
    g_autofree char *dir = g_build_filename(g_get_user_data_dir(), "term", NULL);
    g_autoptr(GError) error = NULL;
    g_autofree char *scheme = NULL, *ini = NULL, *copied = NULL, *name = NULL;
    const char *programs[] = { "qterminal", NULL };

    fake_programs(programs);
    g_assert_true(lxp_scheme_generate(theme, dir, &error));
    g_assert_true(lxp_theme_save(theme, dir, &error));
    name = lxp_theme_get_string(theme, "Terminal", "color_scheme");
    g_assert_cmpstr(name, ==, "lxp-term");
    scheme = read_file(dir, "data/qtermwidget6/color-schemes/lxp-term.colorscheme");
    g_assert_nonnull(strstr(scheme, "[Color7Intense]"));
    g_assert_nonnull(strstr(scheme, "[Background]\nBold=false\nColor=255,255,255\n"));

    write_file(g_get_user_config_dir(), "qterminal.org/qterminal.ini",
               "[General]\ncolorScheme=Linux\nfontFamily=Mono\n");
    {
        g_autoptr(LxpTheme) loaded = lxp_theme_load(dir, &error);
        g_assert_true(terminal->check(loaded, &error));
        g_assert_true(terminal->apply(loaded, &error));
        g_assert_no_error(error);
    }
    ini = read_file(g_get_user_config_dir(), "qterminal.org/qterminal.ini");
    g_assert_cmpstr(ini, ==, "[General]\ncolorScheme=lxp-term\nfontFamily=Mono\n");
    copied = read_file(g_get_user_data_dir(), "qtermwidget6/color-schemes/lxp-term.colorscheme");
    g_assert_cmpstr(copied, ==, scheme);

    lxp_theme_set_string(bad, "Terminal", "color_scheme", "../../evil");
    g_assert_false(terminal->check(bad, &error));
    g_assert_error(error, LXP_ERROR, LXP_ERROR_INVALID);
}

static void test_terminal_overrides(void)
{
    g_autoptr(LxpTheme) theme = scheme_theme("Ansi");
    g_autoptr(LxpTheme) saved = lxp_theme_new("Saved");
    g_autofree char *dir = g_build_filename(g_get_user_data_dir(), "ansi", NULL);
    g_autoptr(GError) error = NULL;
    g_autofree char *scheme = NULL, *red = NULL, *green = NULL;
    LxpScheme s;
    LxpSchemeColors c;
    LxpColor derived[LXP_ANSI_COLORS];

    /* red picked, bright green picked, the rest derived */
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "ansi1", "#ff0000");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "ansi10", "#00ff00");
    g_assert_true(lxp_scheme_load(theme, &s, &error));
    g_assert_true(s.has_ansi[1] && s.has_ansi[10] && !s.has_ansi[2]);
    lxp_scheme_derive(&s, &c);
    lxp_terminal_palette(&c, derived);
    g_assert_cmpfloat(c.ansi[2].g, ==, derived[2].g);
    lxp_scheme_save(&s, saved);
    red = lxp_theme_get_string(saved, LXP_SCHEME_GROUP, "ansi1");
    green = lxp_theme_get_string(saved, LXP_SCHEME_GROUP, "ansi10");
    g_assert_cmpstr(red, ==, "#ff0000");
    g_assert_cmpstr(green, ==, "#00ff00");
    {
        g_autofree char *none = lxp_theme_get_string(saved, LXP_SCHEME_GROUP, "ansi2");
        g_assert_null(none);
    }
    lxp_scheme_clear(&s);

    g_assert_true(lxp_scheme_generate(theme, dir, &error));
    g_assert_no_error(error);
    scheme = read_file(dir, "data/qtermwidget6/color-schemes/lxp-ansi.colorscheme");
    g_assert_nonnull(strstr(scheme, "[Color1]\nBold=false\nColor=255,0,0\n"));
    g_assert_nonnull(strstr(scheme, "[Color2Intense]\nBold=false\nColor=0,255,0\n"));
    g_assert_null(strstr(scheme, "[Color2]\nBold=false\nColor=0,255,0\n"));

    /* kept: no scheme file, and the stale generated name is dropped... */
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "generate", "openbox;gtk;lxqt;kvantum");
    g_assert_true(lxp_scheme_generate(theme, dir, &error));
    {
        g_autofree char *file = g_build_filename(dir, "data/qtermwidget6/color-schemes/"
                                                 "lxp-ansi.colorscheme", NULL);
        g_autofree char *name = lxp_theme_get_string(theme, "Terminal", "color_scheme");
        g_assert_false(g_file_test(file, G_FILE_TEST_EXISTS));
        g_assert_null(name);
    }
    /* ...while an installed one stays */
    lxp_theme_set_string(theme, "Terminal", "color_scheme", "Linux");
    g_assert_true(lxp_scheme_generate(theme, dir, &error));
    {
        g_autofree char *name = lxp_theme_get_string(theme, "Terminal", "color_scheme");
        g_assert_cmpstr(name, ==, "Linux");
    }
}

static void test_variant(void)
{
    g_autoptr(LxpTheme) theme = scheme_theme("Duo");
    g_autofree char *dir = g_build_filename(g_get_user_data_dir(), "duo", NULL);
    g_autoptr(GError) error = NULL;
    g_autofree char *variant_dir = NULL;
    g_autoptr(LxpTheme) variant = NULL;
    LxpScheme s;
    LxpSchemeColors c;
    g_autofree char *openbox = NULL;

    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "variant", "true");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "ansi1", "#ff0000");
    g_assert_true(lxp_theme_generate(theme, dir, 64, 36, &error));
    g_assert_true(lxp_theme_save(theme, dir, &error));
    g_assert_no_error(error);

    variant_dir = lxp_theme_variant_dir(dir);
    g_assert_nonnull(variant_dir);
    variant = lxp_theme_load(variant_dir, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(lxp_theme_get_name(variant), ==, "Duo Dark");
    openbox = lxp_theme_get_string(variant, "Openbox", "theme");
    g_assert_cmpstr(openbox, ==, "lxp-duo-dark");

    /* flipped into comfortable ranges: no black backgrounds, readable text */
    g_assert_true(lxp_scheme_load(variant, &s, &error));
    g_assert_false(s.has_ansi[1]);  /* picked for the light background */
    lxp_scheme_derive(&s, &c);
    g_assert_true(c.dark);
    g_assert_cmpfloat(lxp_color_luminance(&c.bg), >, 0.005);
    g_assert_cmpfloat(lxp_color_contrast(&c.fg, &c.bg), >, 7);
    lxp_scheme_clear(&s);

    /* turning it off removes it */
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "variant", NULL);
    g_assert_true(lxp_theme_generate(theme, dir, 64, 36, &error));
    g_assert_null(lxp_theme_variant_dir(dir));
}

static void test_folder_overlay(void)
{
    const char *data = g_get_user_data_dir();
    g_autofree char *out = g_build_filename(data, "overlay", NULL);
    g_autoptr(GError) error = NULL;
    g_autofree char *svg = NULL, *index = NULL, *other = NULL;
    LxpColor orange;

    write_file(data, "icons/Base/index.theme",
               "[Icon Theme]\nName=Base\nDirectories=places/48,apps/48\n\n"
               "[places/48]\nSize=48\nContext=Places\nType=Fixed\n\n"
               "[apps/48]\nSize=48\nContext=Applications\nType=Fixed\n");
    write_file(data, "icons/Base/places/48/folder.svg",
               "<svg><rect fill=\"#3daee9\"/><rect fill=\"#1d99f3\"/><rect fill=\"#ffffff\"/></svg>");
    write_file(data, "icons/Base/places/48/user-trash.svg", "<svg fill=\"#3daee9\"/>");
    write_file(data, "icons/Base/apps/48/folder-app.svg", "<svg fill=\"#3daee9\"/>");

    g_assert_true(lxp_color_parse("#d08770", &orange));
    g_assert_true(lxp_icons_folder_overlay("Base", &orange, "lxp-x", out, &error));
    g_assert_no_error(error);

    svg = read_file(out, "places/48/folder.svg");
    g_assert_null(strstr(svg, "#3daee9"));        /* the blue is gone... */
    g_assert_nonnull(strstr(svg, "#ffffff"));      /* ...neutral colors stay */
    {
        g_autofree char *trash = g_build_filename(out, "places/48/user-trash.svg", NULL);
        g_assert_false(g_file_test(trash, G_FILE_TEST_EXISTS));
    }
    other = g_build_filename(out, "apps", NULL);
    g_assert_false(g_file_test(other, G_FILE_TEST_EXISTS));
    index = read_file(out, "index.theme");
    g_assert_nonnull(strstr(index, "Inherits=Base,hicolor"));
    g_assert_nonnull(strstr(index, "Directories=places/48"));

    g_assert_false(lxp_icons_folder_overlay("Missing", &orange, "lxp-y", out, &error));
    g_assert_error(error, LXP_ERROR, LXP_ERROR_NOT_FOUND);
}

static void test_surprise(void)
{
    g_autoptr(GRand) rand = g_rand_new_with_seed(42);

    for (int i = 0; i < 8; i++) {
        g_autoptr(LxpTheme) theme = lxp_theme_new("x");
        g_autofree char *name = lxp_surprise(theme, rand, i % 2);
        g_autofree char *dir = g_strdup_printf("%s/surprise-%d", g_get_user_data_dir(), i);
        g_autoptr(GError) error = NULL;
        LxpScheme s;
        LxpSchemeColors c;

        g_assert_nonnull(strchr(name, ' '));
        g_assert_true(lxp_scheme_load(theme, &s, &error));
        lxp_scheme_derive(&s, &c);
        g_assert_cmpfloat(lxp_color_contrast(&c.fg, &c.bg), >=, 7);
        g_assert_cmpfloat(lxp_color_contrast(&c.accent_fg, &c.accent), >=, 3);
        lxp_scheme_clear(&s);
        g_assert_true(lxp_theme_generate(theme, dir, 64, 36, &error));
        g_assert_no_error(error);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func("/tier3/terminal", test_terminal);
    g_test_add_func("/tier3/terminal-overrides", test_terminal_overrides);
    g_test_add_func("/tier3/variant", test_variant);
    g_test_add_func("/tier3/folder-overlay", test_folder_overlay);
    g_test_add_func("/tier3/surprise", test_surprise);
    return g_test_run();
}
