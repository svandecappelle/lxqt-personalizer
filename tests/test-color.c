/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-color.h"
#include "lxp-themerc.h"

static void assert_color(const char *text, const char *hex)
{
    LxpColor c;
    g_autofree char *out = NULL;

    if (!lxp_color_parse(text, &c))
        g_error("failed to parse “%s”", text);
    out = lxp_color_to_hex(&c);
    g_assert_cmpstr(out, ==, hex);
}

static void test_parse(void)
{
    LxpColor c;

    assert_color("#fff", "#ffffff");
    assert_color("#c4a1c1", "#c4a1c1");
    assert_color(" #C4A1C1 ", "#c4a1c1");
    assert_color("rgb:ff/80/00", "#ff8000");
    assert_color("rgb:f/8/0", "#ff8800");
    assert_color("rgb:ffff/0000/8080", "#ff0080");
    assert_color("gray50", "#808080");
    assert_color("grey0", "#000000");
    assert_color("White", "#ffffff");

    g_assert_true(lxp_color_parse("#11223380", &c));
    g_assert_cmpfloat_with_epsilon(c.a, 128 / 255.0, 1e-9);

    g_assert_false(lxp_color_parse("#12345", &c));
    g_assert_false(lxp_color_parse("rgb:1/2", &c));
    g_assert_false(lxp_color_parse("nosuchcolor", &c));
    g_assert_false(lxp_color_parse("gray101", &c));
    g_assert_false(lxp_color_parse(NULL, &c));
}

static void test_qt(void)
{
    LxpColor c;
    g_autofree char *hex = NULL;

    g_assert_true(lxp_qt_color_parse(
        "@Variant(\\0\\0\\0\\x43\\x1\\xff\\xff\\xff\\xff\\0\\0\\x80\\x80\\0\\0)", &c));
    hex = lxp_color_to_hex(&c);
    g_assert_cmpstr(hex, ==, "#ff0080");
    g_assert_cmpfloat(c.a, ==, 1.0);

    /* spec 0: invalid color, i.e. "use the theme default" */
    g_assert_false(lxp_qt_color_parse(
        "@Variant(\\0\\0\\0\\x43\\0\\xff\\xff\\0\\0\\0\\0\\0\\0\\0\\0)", &c));
    g_assert_false(lxp_qt_color_parse("@Variant(\\0\\0)", &c));
    g_assert_true(lxp_qt_color_parse("#100e23", &c));
    g_assert_false(lxp_qt_color_parse("", &c));
}

static void test_luminance(void)
{
    LxpColor black = { 0, 0, 0, 1 }, white = { 1, 1, 1, 1 };
    LxpColor mid = lxp_color_mix(&black, &white, 0.5);

    g_assert_cmpfloat(lxp_color_luminance(&black), ==, 0.0);
    g_assert_cmpfloat_with_epsilon(lxp_color_luminance(&white), 1.0, 1e-9);
    g_assert_cmpfloat_with_epsilon(mid.r, 0.5, 1e-9);
}

static const char arrondie[] =
    "! comment\n"
    "window.active.title.bg.color: #c4a1c1\n"
    "window.*.label.text.color: #474c57\n"
    "border.width: 0\n"
    "padding.width: 8\n"
    "window.*.title.bg: Solid Flat\n"
    "window.*.*.bg: Parentrelative\n"
    "menu.*.bg.color: #ffffff\n"
    "menu.items.text.color: #474c57\n"
    "*.justify: left\n"
    "window.label.text.justify: center\n"
    "dup.key: first\n"
    "dup.key: second\n"
    "a.?.c: any\n"
    "a.b.c: name\n";

static void test_themerc(void)
{
    g_autoptr(LxpThemerc) rc = lxp_themerc_new_from_data(arrondie);
    LxpColor c;
    g_autofree char *hex = NULL;

    g_assert_cmpstr(lxp_themerc_lookup(rc, "window.active.title.bg.color"), ==, "#c4a1c1");
    g_assert_cmpstr(lxp_themerc_lookup(rc, "window.inactive.label.text.color"), ==, "#474c57");
    /* literal "title" beats a second wildcard */
    g_assert_cmpstr(lxp_themerc_lookup(rc, "window.active.title.bg"), ==, "Solid Flat");
    g_assert_cmpstr(lxp_themerc_lookup(rc, "window.active.label.bg"), ==, "Parentrelative");
    g_assert_cmpstr(lxp_themerc_lookup(rc, "menu.items.active.bg.color"), ==, "#ffffff");
    g_assert_cmpstr(lxp_themerc_lookup(rc, "window.label.text.justify"), ==, "center");
    g_assert_cmpstr(lxp_themerc_lookup(rc, "menu.title.text.justify"), ==, "left");
    g_assert_cmpstr(lxp_themerc_lookup(rc, "dup.key"), ==, "second");
    g_assert_cmpstr(lxp_themerc_lookup(rc, "a.b.c"), ==, "name");
    g_assert_cmpstr(lxp_themerc_lookup(rc, "a.x.c"), ==, "any");
    g_assert_null(lxp_themerc_lookup(rc, "osd.bg.color"));

    g_assert_cmpint(lxp_themerc_int(rc, "border.width", 1), ==, 0);
    g_assert_cmpint(lxp_themerc_int(rc, "padding.height", 3), ==, 3);

    g_assert_true(lxp_themerc_color(rc, &c, "window.active.border.color",
                                    "window.active.title.bg.color", NULL));
    hex = lxp_color_to_hex(&c);
    g_assert_cmpstr(hex, ==, "#c4a1c1");
    g_assert_false(lxp_themerc_color(rc, &c, "nope.color", NULL));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/color/parse", test_parse);
    g_test_add_func("/color/qt", test_qt);
    g_test_add_func("/color/luminance", test_luminance);
    g_test_add_func("/color/themerc", test_themerc);
    return g_test_run();
}
