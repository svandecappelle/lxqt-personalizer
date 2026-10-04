/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-backend.h"
#include "lxp-error.h"
#include "test-util.h"

static void assert_value(LxpTheme *theme, const char *group, const char *key,
                         const char *expected)
{
    g_autofree char *value = lxp_theme_get_string(theme, group, key);
    g_assert_cmpstr(value, ==, expected);
}

static void test_lxqt_layers(void)
{
    const LxpBackend *lxqt = lxp_backend_find("lxqt");
    g_autoptr(LxpTheme) theme = lxp_theme_new("t");
    g_autoptr(GError) error = NULL;
    GKeyFile *kf;
    g_auto(GStrv) palette = NULL;

    install_fixture("xdg-data/lxqt/lxqt.conf", g_get_system_data_dirs()[0],
                    "lxqt/lxqt.conf");
    install_fixture("config/lxqt/lxqt.conf", g_get_user_config_dir(),
                    "lxqt/lxqt.conf");

    g_assert_nonnull(lxqt);
    g_assert_true(lxqt->capture(theme, &error));
    g_assert_no_error(error);

    /* user file wins, system file fills the gaps */
    assert_value(theme, "LXQt", "theme", "TrelliumRC3");
    assert_value(theme, "LXQt", "style", "Fusion");
    assert_value(theme, "LXQt", "icon_theme", "breeze");
    assert_value(theme, "LXQt", "icon_follow_color_scheme", "true");
    assert_value(theme, "LXQt", "font", "Sans Serif,10,-1,5,50,0,0,0,0,0");
    assert_value(theme, "LXQt Palette", "highlight_color", "#aa00ff");

    /* internal LXQt keys are not part of a theme */
    kf = lxp_theme_get_keyfile(theme);
    g_assert_false(g_key_file_has_key(kf, "LXQt", "__userfile__", NULL));
    palette = g_key_file_get_keys(kf, "LXQt Palette", NULL, NULL);
    g_assert_cmpuint(g_strv_length(palette), ==, 3);
}

static void test_lxqt_no_config(void)
{
    g_autoptr(LxpTheme) theme = lxp_theme_new("t");
    g_autoptr(GError) error = NULL;

    g_assert_true(lxp_backend_find("lxqt")->capture(theme, &error));
    g_assert_no_error(error);
    g_assert_false(g_key_file_has_group(lxp_theme_get_keyfile(theme), "LXQt"));
}

static void test_openbox(void)
{
    const LxpBackend *openbox = lxp_backend_find("openbox");
    g_autoptr(LxpTheme) theme = lxp_theme_new("t");
    g_autoptr(GError) error = NULL;
    g_auto(GStrv) active = NULL;
    g_auto(GStrv) menu = NULL;
    const char *active_expected[] = { "Bitstream Vera Sans", "11", "Bold", "Normal", NULL };
    const char *menu_expected[] = { "Noto Sans", "10", "", "", NULL };

    install_fixture("config/openbox/rc.xml", g_get_user_config_dir(),
                    "openbox/rc.xml");

    g_assert_true(openbox->capture(theme, &error));
    g_assert_no_error(error);
    assert_value(theme, "Openbox", "theme", "Arrondie-Violet");
    assert_value(theme, "Openbox", "title_layout", "NLIMC");
    assert_value(theme, "Openbox", "keep_border", "yes");

    active = lxp_theme_get_string_list(theme, "Openbox", "font_ActiveWindow");
    g_assert_cmpstrv(active, active_expected);
    menu = lxp_theme_get_string_list(theme, "Openbox", "font_MenuItem");
    g_assert_cmpstrv(menu, menu_expected);
}

static void test_openbox_missing(void)
{
    g_autoptr(LxpTheme) theme = lxp_theme_new("t");
    g_autoptr(GError) error = NULL;

    g_assert_false(lxp_backend_find("openbox")->capture(theme, &error));
    g_assert_error(error, LXP_ERROR, LXP_ERROR_NOT_FOUND);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func("/backends/lxqt/layers", test_lxqt_layers);
    g_test_add_func("/backends/lxqt/no-config", test_lxqt_no_config);
    g_test_add_func("/backends/openbox/capture", test_openbox);
    g_test_add_func("/backends/openbox/missing", test_openbox_missing);
    return g_test_run();
}
