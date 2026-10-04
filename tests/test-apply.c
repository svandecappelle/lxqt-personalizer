/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-apply.h"
#include "lxp-backend.h"
#include "lxp-error.h"
#include "lxp-snapshot.h"
#include "test-util.h"

#include <string.h>

static const char *const config_fixtures[] = {
    "lxqt/lxqt.conf", "lxqt/session.conf", "lxqt/panel.conf",
    "openbox/rc.xml", "gtk-3.0/settings.ini", "pcmanfm-qt/lxqt/settings.conf",
    NULL,
};
static const char *const home_fixtures[] = { ".gtkrc-2.0", ".Xresources", NULL };

/* A fake but complete LXQt + Openbox desktop in the isolated dirs. */
static void setup_desktop(void)
{
    const char *config = g_get_user_config_dir();
    const char *data = g_get_user_data_dir();
    const char *programs[] = { "openbox", "pcmanfm-qt", "lxqt-panel", "lxqt-session", NULL };

    g_assert_true(strstr(g_get_home_dir(), "test_") != NULL);

    for (const char *const *f = config_fixtures; *f; f++) {
        g_autofree char *src = g_build_filename("config", *f, NULL);
        install_fixture(src, config, *f);
    }
    for (const char *const *f = home_fixtures; *f; f++) {
        g_autofree char *src = g_build_filename("home", *f, NULL);
        install_fixture(src, g_get_home_dir(), *f);
    }
    fake_programs(programs);

    /* Installed assets referenced by the test theme. */
    write_file(data, "lxqt/themes/nord/lxqt-panel.qss", "");
    write_file(data, "icons/Papirus/index.theme", "[Icon Theme]\nName=Papirus\n");
    write_file(data, "icons/Bibata/cursors/left_ptr", "");
    write_file(data, "themes/Nordic/openbox-3/themerc", "");
    write_file(data, "themes/Nordic/gtk-3.0/gtk.css", "");
}

/* Every section filled, saved with a bundled wallpaper. */
static LxpTheme *make_theme(const char *openbox_theme)
{
    g_autofree char *dir = g_build_filename(g_get_user_data_dir(), "nord-theme", NULL);
    LxpTheme *theme = lxp_theme_new("Nord");
    const char *font[] = { "Noto Sans", "10", "Bold", "Normal", NULL };
    g_autoptr(GError) error = NULL;

    lxp_theme_set_string(theme, "LXQt", "theme", "nord");
    lxp_theme_set_string(theme, "LXQt", "icon_theme", "Papirus");
    lxp_theme_set_string(theme, "LXQt", "font", "Noto Sans,10,-1,5,50,0,0,0,0,0");
    lxp_theme_set_string(theme, "LXQt Palette", "window_color", "#2e3440");
    lxp_theme_set_string(theme, "Openbox", "theme", openbox_theme);
    lxp_theme_set_string_list(theme, "Openbox", "font_MenuItem", font);
    lxp_theme_set_string(theme, "GTK", "theme", "Nordic");
    lxp_theme_set_string(theme, "GTK", "icon_theme", "Papirus");
    lxp_theme_set_string(theme, "GTK", "prefer_dark", "true");
    lxp_theme_set_string(theme, "Cursor", "theme", "Bibata");
    lxp_theme_set_string(theme, "Cursor", "size", "24");
    lxp_theme_set_string(theme, "Wallpaper", "file", "wallpaper.png");
    lxp_theme_set_string(theme, "Wallpaper", "mode", "fit");
    lxp_theme_set_string(theme, "Panel", "position", "Top");
    lxp_theme_set_string(theme, "Panel", "opacity", "80");
    lxp_theme_set_string(theme, "Panel", "font-color",
                         "@Variant(\\0\\0\\0\\x43\\x1\\xff\\xff)");

    write_file(dir, "wallpaper.png", "PNG");
    lxp_theme_save(theme, dir, &error);
    g_assert_no_error(error);
    return theme;
}

static void assert_contains(const char *dir, const char *relpath, const char *needle)
{
    g_autofree char *data = read_file(dir, relpath);

    if (!data || !strstr(data, needle))
        g_error("%s/%s does not contain “%s”:\n%s", dir, relpath, needle,
                data ? data : "(missing)");
}

static void assert_lacks(const char *dir, const char *relpath, const char *needle)
{
    g_autofree char *data = read_file(dir, relpath);

    if (data && strstr(data, needle))
        g_error("%s/%s unexpectedly contains “%s”:\n%s", dir, relpath, needle, data);
}

static void assert_fixtures_untouched(void)
{
    for (const char *const *f = config_fixtures; *f; f++) {
        g_autofree char *rel = g_build_filename("config", *f, NULL);
        g_autofree char *orig = read_file(TEST_DATA_DIR, rel);
        g_autofree char *now = read_file(g_get_user_config_dir(), *f);
        g_assert_cmpstr(now, ==, orig);
    }
    for (const char *const *f = home_fixtures; *f; f++) {
        g_autofree char *rel = g_build_filename("home", *f, NULL);
        g_autofree char *orig = read_file(TEST_DATA_DIR, rel);
        g_autofree char *now = read_file(g_get_home_dir(), *f);
        g_assert_cmpstr(now, ==, orig);
    }
}

static void test_apply_and_rollback(void)
{
    const char *config = g_get_user_config_dir();
    const char *home = g_get_home_dir();
    g_autoptr(LxpTheme) theme = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree char *id = NULL;
    g_autofree char *wallpaper = NULL;

    setup_desktop();
    theme = make_theme("Nordic");
    id = lxp_apply_theme(theme, NULL, LXP_APPLY_DEFAULT, &error);
    g_assert_no_error(error);
    g_assert_nonnull(id);

    /* LXQt: values written, palette replaced, unrelated keys kept */
    assert_contains(config, "lxqt/lxqt.conf", "theme=nord\n");
    assert_contains(config, "lxqt/lxqt.conf", "icon_theme=Papirus\n");
    assert_contains(config, "lxqt/lxqt.conf", "font=\"Noto Sans,10,-1,5,50,0,0,0,0,0\"\n");
    assert_contains(config, "lxqt/lxqt.conf", "[Palette]\nwindow_color=#2e3440\n\n[Qt]\n");
    assert_lacks(config, "lxqt/lxqt.conf", "highlight_color");
    assert_contains(config, "lxqt/lxqt.conf", "__userfile__=true\n");
    assert_contains(config, "lxqt/lxqt.conf", "__theme_updated__=");
    assert_contains(config, "lxqt/lxqt.conf", "style=Fusion\n");

    /* Openbox: edited in place, comments kept, missing font elements added */
    assert_contains(config, "openbox/rc.xml", "<name>Nordic</name>");
    assert_contains(config, "openbox/rc.xml", "<!-- a comment -->");
    assert_contains(config, "openbox/rc.xml", "<name>Noto Sans</name>");
    assert_contains(config, "openbox/rc.xml", "<weight>Bold</weight>");
    assert_contains(config, "openbox/rc.xml", "xmlns=\"http://openbox.org/3.4/rc\"");

    /* GTK: formatting kept; GTK 4 only gets the theme if it supports it */
    assert_contains(config, "gtk-3.0/settings.ini", "# Created by lxqt-config-appearance");
    assert_contains(config, "gtk-3.0/settings.ini", "gtk-theme-name = Nordic\n");
    assert_contains(config, "gtk-3.0/settings.ini", "gtk-application-prefer-dark-theme = 1\n");
    assert_contains(config, "gtk-4.0/settings.ini", "gtk-icon-theme-name=Papirus\n");
    assert_lacks(config, "gtk-4.0/settings.ini", "gtk-theme-name");
    assert_contains(home, ".gtkrc-2.0", "gtk-theme-name = \"Nordic\"\n");

    /* Cursor: every place it is read from */
    assert_contains(config, "lxqt/session.conf", "cursor_theme=Bibata\n");
    assert_contains(config, "lxqt/session.conf", "cursor_size=24\n");
    assert_contains(home, ".icons/default/index.theme", "Inherits=Bibata\n");
    assert_contains(home, ".Xresources", "Xcursor.theme: Bibata\n");
    assert_contains(home, ".Xresources", "URxvt.font: xft:Mono:size=10\n");
    assert_contains(config, "gtk-3.0/settings.ini", "gtk-cursor-theme-name = Bibata\n");
    assert_contains(home, ".gtkrc-2.0", "gtk-cursor-theme-name = \"Bibata\"\n");

    /* Wallpaper: relative path resolved against the theme directory */
    wallpaper = g_strdup_printf("Wallpaper=%s/wallpaper.png\n", lxp_theme_get_dir(theme));
    assert_contains(config, "pcmanfm-qt/lxqt/settings.conf", wallpaper);
    assert_contains(config, "pcmanfm-qt/lxqt/settings.conf", "WallpaperMode=fit\n");
    assert_contains(config, "pcmanfm-qt/lxqt/settings.conf", "WorkAreaMargins=12, 12, 12, 12\n");

    /* Panel: look changed, plugins untouched */
    assert_contains(config, "lxqt/panel.conf", "position=Top\n");
    assert_contains(config, "lxqt/panel.conf", "opacity=80\n");
    assert_contains(config, "lxqt/panel.conf", "font-color=@Variant(\\0\\0\\0\\x43\\x1\\xff\\xff)\n");
    assert_contains(config, "lxqt/panel.conf", "apps\\1\\desktop=/usr/share/applications/qterminal.desktop\n");

    /* Rollback restores every file byte for byte, and removes new ones */
    g_assert_true(lxp_rollback(id, FALSE, &error));
    g_assert_no_error(error);
    assert_fixtures_untouched();
    g_assert_null(read_file(config, "gtk-4.0/settings.ini"));
    g_assert_null(read_file(home, ".icons/default/index.theme"));
    {
        g_autofree char *gtk4 = g_build_filename(config, "gtk-4.0", NULL);
        g_autofree char *icons = g_build_filename(home, ".icons", NULL);
        g_assert_false(g_file_test(gtk4, G_FILE_TEST_EXISTS));
        g_assert_false(g_file_test(icons, G_FILE_TEST_EXISTS));
    }
}

static void test_check_failure_writes_nothing(void)
{
    g_autoptr(LxpTheme) theme = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) snapshots = NULL;
    char *id;

    setup_desktop();
    theme = make_theme("NotInstalled");
    id = lxp_apply_theme(theme, NULL, LXP_APPLY_DEFAULT, &error);
    g_assert_null(id);
    g_assert_error(error, LXP_ERROR, LXP_ERROR_NOT_FOUND);
    g_assert_true(g_str_has_prefix(error->message, "openbox: "));

    snapshots = lxp_snapshot_list();
    g_assert_cmpuint(snapshots->len, ==, 0);
    assert_fixtures_untouched();
}

static void test_failure_restores(void)
{
    g_autoptr(LxpTheme) theme = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree char *orig_lxqt = NULL;
    g_autofree char *now_lxqt = NULL;
    char *id;

    setup_desktop();
    theme = make_theme("Nordic");
    /* lxqt applies first, then openbox fails on a broken rc.xml */
    write_file(g_get_user_config_dir(), "openbox/rc.xml", "<openbox_config><theme>");
    orig_lxqt = read_file(g_get_user_config_dir(), "lxqt/lxqt.conf");

    id = lxp_apply_theme(theme, NULL, LXP_APPLY_DEFAULT, &error);
    g_assert_null(id);
    g_assert_error(error, LXP_ERROR, LXP_ERROR_INVALID);

    now_lxqt = read_file(g_get_user_config_dir(), "lxqt/lxqt.conf");
    g_assert_cmpstr(now_lxqt, ==, orig_lxqt);
}

static void test_only(void)
{
    g_autoptr(LxpTheme) theme = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree char *id = NULL;
    const char *only[] = { "panel", NULL };
    g_autoptr(LxpSnapshotInfo) info = NULL;

    setup_desktop();
    /* checks of unselected backends must not run */
    theme = make_theme("NotInstalled");
    id = lxp_apply_theme(theme, only, LXP_APPLY_DEFAULT, &error);
    g_assert_no_error(error);

    assert_contains(g_get_user_config_dir(), "lxqt/panel.conf", "position=Top\n");
    assert_contains(g_get_user_config_dir(), "lxqt/lxqt.conf", "theme=TrelliumRC3\n");
    info = lxp_snapshot_info_load(id, &error);
    g_assert_cmpuint(info->n_files, ==, 1);
}

static void test_capture(void)
{
    g_autoptr(LxpTheme) theme = lxp_theme_new("captured");
    g_autoptr(GError) error = NULL;
    static const struct { const char *group, *key, *value; } expected[] = {
        { "GTK",       "theme",      "Breeze-Dark" },
        { "GTK",       "font",       "Sans 11" },
        { "Cursor",    "theme",      "Qogir-dark" },
        { "Cursor",    "size",       "18" },
        { "Wallpaper", "file",       "/usr/share/backgrounds/default.png" },
        { "Wallpaper", "mode",       "zoom" },
        { "Panel",     "position",   "Bottom" },
        { "Panel",     "background-color",
          "@Variant(\\0\\0\\0\\x43\\0\\xff\\xff\\0\\0\\0\\0\\0\\0\\0\\0)" },
    };

    setup_desktop();
    {
        const char *ids[] = { "gtk", "cursor", "wallpaper", "panel", NULL };
        for (const char *const *i = ids; *i; i++) {
            const LxpBackend *b = lxp_backend_find(*i);
            g_assert_true(b->capture(theme, &error));
            g_assert_no_error(error);
        }
    }

    for (gsize i = 0; i < G_N_ELEMENTS(expected); i++) {
        g_autofree char *value = lxp_theme_get_string(theme, expected[i].group,
                                                      expected[i].key);
        g_assert_cmpstr(value, ==, expected[i].value);
    }
    g_assert_false(g_key_file_has_key(lxp_theme_get_keyfile(theme), "GTK",
                                      "prefer_dark", NULL));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func("/apply/apply-and-rollback", test_apply_and_rollback);
    g_test_add_func("/apply/check-failure", test_check_failure_writes_nothing);
    g_test_add_func("/apply/failure-restores", test_failure_restores);
    g_test_add_func("/apply/only", test_only);
    g_test_add_func("/apply/capture", test_capture);
    return g_test_run();
}
