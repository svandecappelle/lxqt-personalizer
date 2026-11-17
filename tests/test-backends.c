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

/* The menu button icon goes to the menu plugin the panel really uses. */
static void test_panel_menu_icon(void)
{
    const LxpBackend *panel = lxp_backend_find("panel");
    g_autofree char *dir = g_build_filename(g_get_user_data_dir(), "menu-theme", NULL);
    g_autofree char *other = g_build_filename(g_get_user_data_dir(), "other.svg", NULL);
    g_autofree char *conf = NULL;
    g_autofree char *stored = NULL;
    g_autoptr(LxpTheme) theme = NULL;
    g_autoptr(LxpTheme) captured = lxp_theme_new("captured");
    g_autoptr(GError) error = NULL;
    const char *icon_line, *end;

    /* as on a fresh LXQt: the plugin list only exists in the system file */
    write_file(g_get_system_config_dirs()[0], "lxqt/panel.conf",
               "[panel1]\nplugins=fancymenu, taskbar\n\n"
               "[fancymenu]\ntype=fancymenu\n\n[taskbar]\ntype=taskbar\n");
    write_file(g_get_user_config_dir(), "lxqt/panel.conf",
               "[General]\n__userfile__=true\n\n[mainmenu]\ntype=mainmenu\n\n"
               "[panel1]\nposition=Bottom\n");
    write_file(dir, "theme.ini", "[Theme]\nName=Menu\n\n[Panel]\nmenu_icon=logo.svg\n");
    write_file(dir, "logo.svg", "<svg xmlns='http://www.w3.org/2000/svg'/>");
    write_file(g_get_user_data_dir(), "other.svg", "<svg xmlns='http://www.w3.org/2000/svg' id='o'/>");

    theme = lxp_theme_load(dir, &error);
    g_assert_no_error(error);
    g_assert_true(panel->check(theme, &error));
    g_assert_true(panel->apply(theme, &error));
    g_assert_no_error(error);

    conf = read_file(g_get_user_config_dir(), "lxqt/panel.conf");
    /* the unused mainmenu section is left alone */
    g_assert_nonnull(strstr(conf, "[mainmenu]\ntype=mainmenu\n\n"));
    g_assert_nonnull(strstr(conf, "[fancymenu]\nownIcon=true\nicon="));
    /* a copy named after its contents, outside the theme */
    icon_line = strstr(conf, "icon=") + strlen("icon=");
    end = strchr(icon_line, '\n');
    stored = g_strndup(icon_line, end - icon_line);
    g_assert_true(g_str_has_prefix(stored, g_get_user_data_dir()));
    g_assert_nonnull(strstr(stored, "/menu-icons/"));
    g_assert_true(g_str_has_suffix(stored, ".svg"));
    g_assert_true(g_file_test(stored, G_FILE_TEST_IS_REGULAR));

    /* captured back as that file */
    g_assert_true(panel->capture(captured, &error));
    assert_value(captured, "Panel", "menu_icon", stored);

    /* "theme" hands the button back to the LXQt theme */
    lxp_theme_set_string(theme, "Panel", "menu_icon", "theme");
    g_assert_true(panel->apply(theme, &error));
    g_clear_pointer(&conf, g_free);
    conf = read_file(g_get_user_config_dir(), "lxqt/panel.conf");
    g_assert_nonnull(strstr(conf, "ownIcon=false\n"));
    g_assert_true(panel->capture(captured, &error));
    assert_value(captured, "Panel", "menu_icon", "theme");

    /* without the key the menu button is not touched */
    lxp_theme_set_string(theme, "Panel", "menu_icon", NULL);
    lxp_theme_set_string(theme, "Panel", "position", "Top");
    g_assert_true(panel->apply(theme, &error));
    g_clear_pointer(&conf, g_free);
    conf = read_file(g_get_user_config_dir(), "lxqt/panel.conf");
    g_assert_nonnull(strstr(conf, "ownIcon=false\n"));
    g_assert_nonnull(strstr(conf, "position=Top\n"));

    /* an absolute file works; what is not an image, or leaves the theme, does not */
    lxp_theme_set_string(theme, "Panel", "menu_icon", other);
    g_assert_true(panel->check(theme, &error));
    lxp_theme_set_string(theme, "Panel", "menu_icon", "theme.ini");
    g_assert_false(panel->check(theme, &error));
    g_assert_error(error, LXP_ERROR, LXP_ERROR_NOT_FOUND);
    g_clear_error(&error);
    lxp_theme_set_string(theme, "Panel", "menu_icon", "../other.svg");
    g_assert_false(panel->check(theme, &error));
    g_clear_error(&error);
    lxp_theme_set_string(theme, "Panel", "menu_icon", "missing.svg");
    g_assert_false(panel->apply(theme, &error));
    g_clear_error(&error);

    /* saving a theme elsewhere takes the image along */
    {
        g_autofree char *copy = g_build_filename(g_get_user_data_dir(), "menu-copy", NULL);
        g_autofree char *bundled = NULL;

        lxp_theme_set_string(theme, "Panel", "menu_icon", other);
        g_assert_true(lxp_theme_bundle_wallpaper(theme, copy, &error));
        g_assert_no_error(error);
        assert_value(theme, "Panel", "menu_icon", "menu-icon.svg");
        bundled = read_file(copy, "menu-icon.svg");
        g_assert_nonnull(strstr(bundled, "id='o'"));
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func("/backends/lxqt/layers", test_lxqt_layers);
    g_test_add_func("/backends/lxqt/no-config", test_lxqt_no_config);
    g_test_add_func("/backends/openbox/capture", test_openbox);
    g_test_add_func("/backends/openbox/missing", test_openbox_missing);
    g_test_add_func("/backends/panel-menu-icon", test_panel_menu_icon);
    return g_test_run();
}
