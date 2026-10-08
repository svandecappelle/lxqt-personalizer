/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-apply.h"
#include "lxp-assets.h"
#include "lxp-catalog.h"
#include "lxp-error.h"
#include "lxp-ini.h"
#include "lxp-pack.h"
#include "lxp-paths.h"
#include "lxp-scheme.h"
#include "test-util.h"

#include <archive.h>
#include <archive_entry.h>
#include <string.h>

static char *path_in(const char *dir, const char *rel)
{
    return g_build_filename(dir, rel, NULL);
}

static gboolean exists(const char *dir, const char *rel)
{
    g_autofree char *p = path_in(dir, rel);
    return g_file_test(p, G_FILE_TEST_EXISTS);
}

/* A generated theme with a wallpaper outside its folder, saved in <dir>. */
static LxpTheme *saved_theme(const char *dir)
{
    LxpTheme *theme = lxp_theme_new("Pack Test");
    g_autofree char *wallpaper = path_in(g_get_user_config_dir(), "pictures/bg.png");
    g_autoptr(GError) error = NULL;

    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "background", "#2e3440");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "surface", "#3b4252");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "foreground", "#eceff4");
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "accent", "#88c0d0");
    write_file(g_get_user_config_dir(), "pictures/bg.png", "PNG");
    lxp_theme_set_string(theme, "Wallpaper", "file", wallpaper);
    g_assert_true(lxp_scheme_generate(theme, dir, &error));
    g_assert_true(lxp_theme_save(theme, dir, &error));
    g_assert_no_error(error);
    return theme;
}

static void test_roundtrip(void)
{
    g_autofree char *src = path_in(g_get_user_config_dir(), "src");
    g_autofree char *archive = path_in(g_get_user_config_dir(), "pack.tar.gz");
    g_autofree char *preview = path_in(g_get_user_config_dir(), "preview.png");
    g_autoptr(LxpTheme) theme = saved_theme(src);
    g_autoptr(LxpTheme) imported = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree char *dir = NULL;
    g_autofree char *again = NULL;
    g_autofree char *wallpaper = NULL;
    g_autofree char *orig_ini = read_file(src, "theme.ini");
    g_autofree char *themes = lxp_paths_themes_dir();
    g_autoptr(GPtrArray) listed = NULL;

    write_file(g_get_user_config_dir(), "preview.png", "PREVIEW");
    g_assert_true(lxp_pack_export(theme, archive, preview, LXP_PACK_DEFAULT, NULL, &error));
    g_assert_no_error(error);

    /* exporting never modifies the original theme */
    {
        g_autofree char *now = read_file(src, "theme.ini");
        g_assert_cmpstr(now, ==, orig_ini);
        g_assert_false(exists(src, "wallpaper.png"));
    }

    dir = lxp_pack_import(archive, LXP_PACK_DEFAULT, &error);
    g_assert_no_error(error);
    g_assert_true(g_str_has_suffix(dir, "/pack-test"));
    imported = lxp_theme_load(dir, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(lxp_theme_get_name(imported), ==, "Pack Test");
    wallpaper = lxp_theme_get_string(imported, "Wallpaper", "file");
    g_assert_cmpstr(wallpaper, ==, "wallpaper.png");
    g_assert_true(exists(dir, "wallpaper.png"));
    g_assert_true(exists(dir, "preview.png"));
    g_assert_true(exists(dir, "data/themes/lxp-pack-test/openbox-3/themerc"));

    /* no second copy without --force, and no leftovers */
    again = lxp_pack_import(archive, LXP_PACK_DEFAULT, &error);
    g_assert_null(again);
    g_assert_error(error, LXP_ERROR, LXP_ERROR_EXISTS);
    g_clear_error(&error);
    again = lxp_pack_import(archive, LXP_PACK_FORCE, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(again, ==, dir);

    listed = lxp_theme_list_installed();
    g_assert_cmpuint(listed->len, ==, 1);
    {
        g_autoptr(GDir) d = g_dir_open(themes, 0, NULL);
        const char *name;
        while ((name = g_dir_read_name(d)))
            g_assert_false(g_str_has_prefix(name, ".import-"));
    }
}

static void test_bundle(void)
{
    g_autofree char *src = path_in(g_get_user_config_dir(), "src");
    g_autofree char *archive = path_in(g_get_user_config_dir(), "pack.tar.gz");
    g_autoptr(LxpTheme) theme = saved_theme(src);
    g_autoptr(GPtrArray) notes = g_ptr_array_new_with_free_func(g_free);
    g_autoptr(GError) error = NULL;
    g_autofree char *dir = NULL;
    g_autofree char *marker = NULL;
    g_autofree char *mine = NULL;

    /* a user-installed Openbox theme the theme uses */
    write_file(g_get_home_dir(), ".themes/Boxy/openbox-3/themerc", "border.width: 3\n");
    lxp_theme_set_string(theme, "Openbox", "theme", "Boxy");
    g_assert_true(lxp_theme_save(theme, src, &error));

    g_assert_true(lxp_pack_export(theme, archive, NULL, LXP_PACK_DEFAULT, notes, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(notes->len, ==, 1);
    g_assert_cmpstr(notes->pdata[0], ==, "Bundled themes “Boxy”");

    dir = lxp_pack_import(archive, LXP_PACK_DEFAULT, &error);
    g_assert_no_error(error);
    marker = read_file(dir, "data/themes/Boxy/.lxp-asset");
    g_assert_cmpstr(marker, ==, LXP_ASSET_IF_MISSING);

    /* the recipient already has a Boxy: it is kept, and that is not an error */
    write_file(g_get_user_data_dir(), "themes/Boxy/openbox-3/themerc", "mine\n");
    {
        g_autoptr(LxpTheme) imported = lxp_theme_load(dir, &error);
        g_assert_true(lxp_assets_install(imported, &error));
        g_assert_no_error(error);
    }
    mine = read_file(g_get_user_data_dir(), "themes/Boxy/openbox-3/themerc");
    g_assert_cmpstr(mine, ==, "mine\n");
}

typedef enum { ENTRY_FILE, ENTRY_DIR, ENTRY_SYMLINK } EntryType;

static void write_archive(const char *path, const char *const *names, const EntryType *types)
{
    struct archive *a = archive_write_new();

    g_assert_true(lxp_ensure_parent_dir(path, NULL));
    archive_write_add_filter_gzip(a);
    archive_write_set_format_pax_restricted(a);
    g_assert_cmpint(archive_write_open_filename(a, path), ==, ARCHIVE_OK);
    for (int i = 0; names[i]; i++) {
        struct archive_entry *e = archive_entry_new();
        const char *content = "[Theme]\nName=Evil\n";

        archive_entry_set_pathname(e, names[i]);
        archive_entry_set_perm(e, 0644);
        if (types[i] == ENTRY_DIR) {
            archive_entry_set_filetype(e, AE_IFDIR);
        } else if (types[i] == ENTRY_SYMLINK) {
            archive_entry_set_filetype(e, AE_IFLNK);
            archive_entry_set_symlink(e, "/etc");
        } else {
            archive_entry_set_filetype(e, AE_IFREG);
            archive_entry_set_size(e, (la_int64_t)strlen(content));
        }
        archive_write_header(a, e);
        if (types[i] == ENTRY_FILE)
            archive_write_data(a, content, strlen(content));
        archive_entry_free(e);
    }
    archive_write_close(a);
    archive_write_free(a);
}

static void test_hostile_archives(void)
{
    static const struct {
        const char *names[3];
        EntryType types[3];
    } cases[] = {
        { { "evil/theme.ini", "../escape", NULL }, { ENTRY_FILE, ENTRY_FILE } },
        { { "evil/theme.ini", "evil/../../escape", NULL }, { ENTRY_FILE, ENTRY_FILE } },
        { { "/tmp/absolute", NULL }, { ENTRY_FILE } },
        { { "evil/theme.ini", "evil/link", NULL }, { ENTRY_FILE, ENTRY_SYMLINK } },
    };
    g_autofree char *archive = path_in(g_get_user_config_dir(), "evil.tar.gz");
    g_autofree char *themes = lxp_paths_themes_dir();

    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        g_autoptr(GError) error = NULL;
        g_autoptr(GPtrArray) listed = NULL;
        char *dir;

        write_archive(archive, cases[i].names, cases[i].types);
        dir = lxp_pack_import(archive, LXP_PACK_DEFAULT, &error);
        g_assert_null(dir);
        g_assert_error(error, LXP_ERROR, LXP_ERROR_INVALID);

        g_assert_false(exists(themes, "escape"));
        g_assert_false(exists(g_get_user_data_dir(), "lxqt-personalizer/escape"));
        listed = lxp_theme_list_installed();
        g_assert_cmpuint(listed->len, ==, 0);
    }

    /* not a theme pack at all */
    {
        const char *names[] = { "readme.txt", NULL };
        EntryType types[] = { ENTRY_FILE };
        g_autoptr(GError) error = NULL;

        write_archive(archive, names, types);
        g_assert_null(lxp_pack_import(archive, LXP_PACK_DEFAULT, &error));
        g_assert_error(error, LXP_ERROR, LXP_ERROR_INVALID);
    }
}

static void test_asset_kinds(void)
{
    g_autofree char *dir = path_in(g_get_user_config_dir(), "kinds");
    g_autoptr(LxpTheme) theme = lxp_theme_new("Kinds");
    g_autoptr(GError) error = NULL;

    g_assert_true(lxp_assets_kind_allowed("themes/Foo"));
    g_assert_true(lxp_assets_kind_allowed("lxqt/themes/Foo"));
    g_assert_true(lxp_assets_kind_allowed("icons/Foo"));
    g_assert_false(lxp_assets_kind_allowed("dbus-1/services/evil"));
    g_assert_false(lxp_assets_kind_allowed("applications/evil"));
    g_assert_false(lxp_assets_kind_allowed("themes"));
    g_assert_false(lxp_assets_kind_allowed("themes/.hidden"));

    /* a D-Bus service would be run as a program: never installed */
    write_file(dir, "data/dbus-1/services/.lxp-asset", "");
    write_file(dir, "data/dbus-1/services/org.evil.service", "[D-BUS Service]\nExec=/bin/true\n");
    g_assert_true(lxp_theme_save(theme, dir, &error));
    g_assert_false(lxp_assets_install(theme, &error));
    g_assert_error(error, LXP_ERROR, LXP_ERROR_INVALID);
    g_assert_false(exists(g_get_user_data_dir(), "dbus-1"));
}

static void test_ini_injection(void)
{
    g_autoptr(LxpIni) ini = lxp_ini_new_from_data("[General]\ntheme=a\n");
    g_autofree char *data = NULL;

    lxp_ini_set(ini, "General", "theme", "b\n[Evil]\nexec=rm -rf ~");
    g_test_expect_message(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "*invalid INI key*");
    lxp_ini_set(ini, "General", "[Evil]x", "1");
    g_test_assert_expected_messages();
    data = lxp_ini_to_data(ini);
    g_assert_cmpstr(data, ==, "[General]\ntheme=b [Evil] exec=rm -rf ~\n");
}

static void test_check_all(void)
{
    g_autoptr(LxpTheme) theme = lxp_theme_new("Needs Stuff");
    g_autoptr(GPtrArray) problems = NULL;
    const char *programs[] = { "openbox", NULL };

    fake_programs(programs);
    lxp_theme_set_string(theme, "Openbox", "theme", "NoSuchOpenbox");
    lxp_theme_set_string(theme, "Cursor", "theme", "NoSuchCursor");
    lxp_theme_set_string(theme, "GTK", "theme", "Adwaita");  /* built in: fine */

    problems = lxp_check_theme_all(theme, NULL);
    g_assert_cmpuint(problems->len, ==, 2);
    g_assert_true(g_str_has_prefix(((GError *)problems->pdata[0])->message, "openbox: "));
    g_assert_true(g_str_has_prefix(((GError *)problems->pdata[1])->message, "cursor: "));
}

static gboolean listed(GPtrArray *names, const char *name)
{
    return g_ptr_array_find_with_equal_func(names, name, g_str_equal, NULL);
}

static void test_catalog(void)
{
    const char *data = g_get_user_data_dir();
    g_autoptr(GPtrArray) openbox = NULL, gtk = NULL, lxqt = NULL, cursors = NULL, styles = NULL;

    write_file(data, "themes/Boxy/openbox-3/themerc", "");
    write_file(g_get_home_dir(), ".themes/Legacy/openbox-3/themerc", "");
    write_file(data, "themes/Keys/gtk-3.0/gtk-keys.css", "");     /* key bindings only */
    write_file(data, "themes/Real/gtk-3.0/gtk.css", "");
    write_file(data, "themes/lxp-generated/openbox-3/themerc", "");
    write_file(data, "lxqt/themes/Frosty/lxqt-panel.qss", "");
    write_file(data, "lxqt/themes/.hidden/lxqt-panel.qss", "");
    write_file(data, "lxqt/themes/backup.zip", "");
    write_file(data, "icons/Pointy/cursors/left_ptr", "");
    write_file(data, "icons/OnlyIcons/index.theme", "");

    openbox = lxp_catalog_list(LXP_CATALOG_OPENBOX);
    g_assert_true(listed(openbox, "Boxy"));
    g_assert_true(listed(openbox, "Legacy"));
    g_assert_false(listed(openbox, "lxp-generated"));
    gtk = lxp_catalog_list(LXP_CATALOG_GTK);
    g_assert_true(listed(gtk, "Real"));
    g_assert_true(listed(gtk, "Adwaita"));
    g_assert_false(listed(gtk, "Keys"));
    lxqt = lxp_catalog_list(LXP_CATALOG_LXQT);
    g_assert_true(listed(lxqt, "Frosty"));
    g_assert_false(listed(lxqt, ".hidden"));
    g_assert_false(listed(lxqt, "backup.zip"));
    cursors = lxp_catalog_list(LXP_CATALOG_CURSORS);
    g_assert_true(listed(cursors, "Pointy"));
    g_assert_false(listed(cursors, "OnlyIcons"));
    styles = lxp_catalog_list(LXP_CATALOG_QT_STYLES);
    g_assert_true(listed(styles, "Fusion"));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func("/pack/roundtrip", test_roundtrip);
    g_test_add_func("/pack/bundle", test_bundle);
    g_test_add_func("/pack/hostile-archives", test_hostile_archives);
    g_test_add_func("/pack/asset-kinds", test_asset_kinds);
    g_test_add_func("/pack/ini-injection", test_ini_injection);
    g_test_add_func("/pack/check-all", test_check_all);
    g_test_add_func("/pack/catalog", test_catalog);
    return g_test_run();
}
