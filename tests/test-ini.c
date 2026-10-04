/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-ini.h"

static const char gtk_ini[] =
    "\n"
    "# Created by lxqt-config-appearance (DO NOT EDIT!)\n"
    "[Settings]\n"
    "gtk-theme-name = Breeze-Dark\n"
    "# comment inside\n"
    "gtk-font-name = Sans 11\n"
    "\n"
    "[Other]\n"
    "a=1\n";

static void test_preserve(void)
{
    g_autoptr(LxpIni) ini = lxp_ini_new_from_data(gtk_ini);
    g_autofree char *data = lxp_ini_to_data(ini);

    g_assert_cmpstr(data, ==, gtk_ini);
    g_assert_cmpstr(lxp_ini_get(ini, "Settings", "gtk-font-name"), ==, "Sans 11");
    g_assert_cmpstr(lxp_ini_get(ini, "Other", "a"), ==, "1");
    g_assert_null(lxp_ini_get(ini, "Settings", "a"));
}

static void test_no_trailing_newline(void)
{
    g_autoptr(LxpIni) ini = lxp_ini_new_from_data("[A]\nx=1");
    g_autofree char *data = NULL;

    lxp_ini_set(ini, "A", "x", "2");
    data = lxp_ini_to_data(ini);
    g_assert_cmpstr(data, ==, "[A]\nx=2");
}

static void test_set(void)
{
    g_autoptr(LxpIni) ini = lxp_ini_new_from_data(gtk_ini);
    g_autofree char *data = NULL;

    lxp_ini_set(ini, "Settings", "gtk-theme-name", "Nordic");
    lxp_ini_set(ini, "Settings", "gtk-icon-theme-name", "Papirus");
    lxp_ini_set(ini, "New", "k", "v");
    data = lxp_ini_to_data(ini);

    g_assert_cmpstr(data, ==,
        "\n"
        "# Created by lxqt-config-appearance (DO NOT EDIT!)\n"
        "[Settings]\n"
        "gtk-theme-name = Nordic\n"
        "# comment inside\n"
        "gtk-font-name = Sans 11\n"
        "gtk-icon-theme-name = Papirus\n"
        "\n"
        "[Other]\n"
        "a=1\n"
        "\n"
        "[New]\n"
        "k = v\n");
}

static void test_root_group(void)
{
    g_autoptr(LxpIni) ini = lxp_ini_new_from_data("# gtkrc\ngtk-a = \"x\"\n");
    g_autoptr(LxpIni) empty = lxp_ini_new();
    g_autofree char *data = NULL;
    g_autofree char *empty_data = NULL;

    g_assert_cmpstr(lxp_ini_get(ini, NULL, "gtk-a"), ==, "\"x\"");
    lxp_ini_set(ini, NULL, "gtk-b", "1");
    data = lxp_ini_to_data(ini);
    g_assert_cmpstr(data, ==, "# gtkrc\ngtk-a = \"x\"\ngtk-b = 1\n");

    lxp_ini_set(empty, "G", "k", "v");
    empty_data = lxp_ini_to_data(empty);
    g_assert_cmpstr(empty_data, ==, "[G]\nk=v\n");
}

static void test_remove(void)
{
    g_autoptr(LxpIni) ini = lxp_ini_new_from_data(gtk_ini);
    g_autofree char *data = NULL;
    g_auto(GStrv) groups = NULL;
    g_auto(GStrv) keys = NULL;

    lxp_ini_remove(ini, "Settings", "gtk-font-name");
    lxp_ini_remove_group(ini, "Other");
    g_assert_false(lxp_ini_has_group(ini, "Other"));
    data = lxp_ini_to_data(ini);
    g_assert_cmpstr(data, ==,
        "\n"
        "# Created by lxqt-config-appearance (DO NOT EDIT!)\n"
        "[Settings]\n"
        "gtk-theme-name = Breeze-Dark\n"
        "# comment inside\n"
        "\n");

    groups = lxp_ini_get_groups(ini);
    g_assert_cmpuint(g_strv_length(groups), ==, 1);
    keys = lxp_ini_get_keys(ini, "Settings");
    g_assert_cmpstr(keys[0], ==, "gtk-theme-name");
    g_assert_null(keys[1]);
}

static void test_qt_quote(void)
{
    struct { const char *plain, *encoded; } cases[] = {
        { "Fusion",                          "Fusion" },
        { "Sans,11,-1,5,50,0,0,0,0,0",       "\"Sans,11,-1,5,50,0,0,0,0,0\"" },
        { "/path/with space/a.png",          "/path/with space/a.png" },
        { "say \"hi\"",                      "say \\\"hi\\\"" },
        { "C:\\dir",                         "C:\\\\dir" },
        { "@literal",                        "@@literal" },
        { " padded ",                        "\" padded \"" },
    };

    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        g_autofree char *encoded = lxp_qt_quote(cases[i].plain);
        g_autofree char *decoded = lxp_qt_unquote(cases[i].encoded);

        g_assert_cmpstr(encoded, ==, cases[i].encoded);
        g_assert_cmpstr(decoded, ==, cases[i].plain);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ini/preserve", test_preserve);
    g_test_add_func("/ini/no-trailing-newline", test_no_trailing_newline);
    g_test_add_func("/ini/set", test_set);
    g_test_add_func("/ini/root-group", test_root_group);
    g_test_add_func("/ini/remove", test_remove);
    g_test_add_func("/ini/qt-quote", test_qt_quote);
    return g_test_run();
}
