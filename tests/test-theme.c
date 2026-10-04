/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-error.h"
#include "lxp-paths.h"
#include "lxp-theme.h"

static void test_slugify(void)
{
    struct { const char *in, *out; } cases[] = {
        { "Nord Night",       "nord-night" },
        { "  Été -- Rose!! ", "ete-rose" },
        { "TrelliumRC3",      "trelliumrc3" },
        { "!!!",              "theme" },
    };

    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        g_autofree char *slug = lxp_theme_slugify(cases[i].in);
        g_assert_cmpstr(slug, ==, cases[i].out);
    }
}

static void test_roundtrip(void)
{
    g_autofree char *themes = lxp_paths_themes_dir();
    g_autofree char *dir = g_build_filename(themes, "nord-night", NULL);
    g_autoptr(LxpTheme) theme = lxp_theme_new("Nord Night");
    g_autoptr(LxpTheme) loaded = NULL;
    g_autoptr(GPtrArray) installed = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree char *resolved = NULL;
    g_autofree char *value = NULL;
    g_auto(GStrv) font = NULL;
    const char *font_in[] = { "Noto Sans", "11", "Bold", "Normal", NULL };

    lxp_theme_set_string(theme, "LXQt", "theme", "nord");
    lxp_theme_set_string_list(theme, "Openbox", "font_ActiveWindow", font_in);
    lxp_theme_save(theme, dir, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(lxp_theme_get_dir(theme), ==, dir);

    resolved = lxp_theme_resolve("Nord Night", &error);
    g_assert_no_error(error);
    g_assert_cmpstr(resolved, ==, dir);

    loaded = lxp_theme_load(resolved, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(lxp_theme_get_name(loaded), ==, "Nord Night");
    value = lxp_theme_get_string(loaded, "LXQt", "theme");
    g_assert_cmpstr(value, ==, "nord");
    font = lxp_theme_get_string_list(loaded, "Openbox", "font_ActiveWindow");
    g_assert_cmpstrv(font, font_in);

    installed = lxp_theme_list_installed();
    g_assert_cmpuint(installed->len, ==, 1);
    g_assert_cmpstr(g_ptr_array_index(installed, 0), ==, "nord-night");
}

static void test_load_errors(void)
{
    g_autofree char *dir = g_build_filename(g_get_user_data_dir(), "bad", NULL);
    g_autofree char *manifest = g_build_filename(dir, LXP_THEME_MANIFEST, NULL);
    g_autoptr(GError) error = NULL;
    LxpTheme *theme;

    g_mkdir_with_parents(dir, 0755);

    g_file_set_contents(manifest, "[Theme]\nComment=no name\n", -1, NULL);
    theme = lxp_theme_load(dir, &error);
    g_assert_null(theme);
    g_assert_error(error, LXP_ERROR, LXP_ERROR_INVALID);
    g_clear_error(&error);

    g_file_set_contents(manifest, "[Theme]\nName=x\nFormatVersion=99\n", -1, NULL);
    theme = lxp_theme_load(dir, &error);
    g_assert_null(theme);
    g_assert_error(error, LXP_ERROR, LXP_ERROR_UNSUPPORTED);
    g_clear_error(&error);

    g_assert_null(lxp_theme_resolve("does-not-exist", &error));
    g_assert_error(error, LXP_ERROR, LXP_ERROR_NOT_FOUND);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func("/theme/slugify", test_slugify);
    g_test_add_func("/theme/roundtrip", test_roundtrip);
    g_test_add_func("/theme/load-errors", test_load_errors);
    return g_test_run();
}
