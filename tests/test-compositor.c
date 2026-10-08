/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-apply.h"
#include "lxp-backend.h"
#include "lxp-error.h"
#include "lxp-libconfig.h"
#include "lxp-preview-model.h"
#include "test-util.h"

#include <string.h>

static const char picom_conf[] =
    "# my picom\n"
    "backend = \"glx\";\n"
    "corner-radius = 4;  # small\n"
    "rounded-corners-exclude = [\n"
    "  \"window_type = 'dock'\",\n"
    "  \"name = 'corner-radius = 99;'\"\n"
    "];\n"
    "wintypes: {\n"
    "  tooltip = { shadow = false; };\n"
    "};\n"
    "shadow: false\n";

static void test_libconfig(void)
{
    g_autoptr(LxpLibconfig) conf = lxp_libconfig_new_from_data(picom_conf);
    g_autofree char *radius = lxp_libconfig_get(conf, "corner-radius");
    g_autofree char *shadow = lxp_libconfig_get(conf, "shadow");
    g_autofree char *backend = lxp_libconfig_get(conf, "backend");
    g_autofree char *missing = lxp_libconfig_get(conf, "fading");
    const char *out;

    g_assert_cmpstr(radius, ==, "4");
    g_assert_cmpstr(shadow, ==, "false");          /* "name: value", no ';' */
    g_assert_cmpstr(backend, ==, "\"glx\"");
    g_assert_null(missing);

    lxp_libconfig_set(conf, "corner-radius", "12");
    lxp_libconfig_set(conf, "shadow", "true");
    lxp_libconfig_set(conf, "fading", "true\nexec = \"evil\";");
    out = lxp_libconfig_to_data(conf);

    g_assert_nonnull(strstr(out, "corner-radius = 12;  # small\n"));
    g_assert_nonnull(strstr(out, "shadow: true\n"));
    /* the list and the nested group are untouched */
    g_assert_nonnull(strstr(out, "\"name = 'corner-radius = 99;'\""));
    g_assert_nonnull(strstr(out, "tooltip = { shadow = false; };"));
    g_assert_nonnull(strstr(out, "# my picom\n"));
    /* a value cannot add statements */
    g_assert_nonnull(strstr(out, "\nfading = true exec = \"evil\" ;\n"));
    g_assert_null(strstr(out, "\nexec"));
}

static void install_picom(gboolean works)
{
    const char *programs[] = { "picom", NULL };
    g_autofree char *bin = g_build_filename(g_get_user_data_dir(), "fake-bin", NULL);

    fake_programs(programs);
    if (!works)
        write_file(bin, "picom", "#!/bin/sh\necho 'libconfig.so.11: missing' >&2\nexit 127\n");
}

static LxpTheme *compositor_theme(const char *radius)
{
    LxpTheme *theme = lxp_theme_new("Round");

    lxp_theme_set_string(theme, "Compositor", "corner_radius", radius);
    lxp_theme_set_string(theme, "Compositor", "shadow", "true");
    lxp_theme_set_string(theme, "Compositor", "shadow_opacity", "0.5");
    return theme;
}

static void test_apply(void)
{
    g_autoptr(LxpTheme) theme = compositor_theme("10");
    g_autoptr(GError) error = NULL;
    g_autofree char *id = NULL;
    g_autofree char *after = NULL;
    const char *only[] = { "compositor", NULL };

    install_picom(TRUE);
    write_file(g_get_user_config_dir(), "picom.conf", picom_conf);

    id = lxp_apply_theme(theme, only, LXP_APPLY_DEFAULT, &error);
    g_assert_no_error(error);
    after = read_file(g_get_user_config_dir(), "picom.conf");
    g_assert_nonnull(strstr(after, "corner-radius = 10;  # small\n"));
    g_assert_nonnull(strstr(after, "shadow: true\n"));
    g_assert_nonnull(strstr(after, "\nshadow-opacity = 0.5;\n"));
    g_assert_nonnull(strstr(after, "rounded-corners-exclude = [\n"));

    /* and back */
    g_assert_true(lxp_rollback(id, FALSE, &error));
    g_free(after);
    after = read_file(g_get_user_config_dir(), "picom.conf");
    g_assert_cmpstr(after, ==, picom_conf);
}

static void test_system_config(void)
{
    g_autoptr(LxpTheme) theme = compositor_theme("8");
    g_autoptr(GError) error = NULL;
    g_autofree char *id = NULL;
    g_autofree char *user = NULL;
    const char *only[] = { "compositor", NULL };

    install_picom(TRUE);
    write_file(g_get_system_config_dirs()[0], "picom.conf", "vsync = true;\n");

    /* the user file replaces the system one, so it starts as a copy of it */
    id = lxp_apply_theme(theme, only, LXP_APPLY_DEFAULT, &error);
    g_assert_no_error(error);
    user = read_file(g_get_user_config_dir(), "picom.conf");
    g_assert_cmpstr(user, ==, "vsync = true;\ncorner-radius = 8;\nshadow = true;\n"
                              "shadow-opacity = 0.5;\n");
}

static void test_check(void)
{
    g_autoptr(LxpTheme) bad = compositor_theme("ten");
    g_autoptr(LxpTheme) good = compositor_theme("10");
    g_autoptr(GPtrArray) problems = NULL;
    const char *only[] = { "compositor", NULL };

    install_picom(TRUE);
    problems = lxp_check_theme_all(bad, only);
    g_assert_cmpuint(problems->len, ==, 1);
    g_assert_error((GError *)problems->pdata[0], LXP_ERROR, LXP_ERROR_INVALID);
    g_clear_pointer(&problems, g_ptr_array_unref);

    /* a picom that cannot start is reported, with its error */
    install_picom(FALSE);
    problems = lxp_check_theme_all(good, only);
    g_assert_cmpuint(problems->len, ==, 1);
    g_assert_nonnull(strstr(((GError *)problems->pdata[0])->message, "does not start"));
    g_assert_nonnull(strstr(((GError *)problems->pdata[0])->message, "libconfig.so.11"));
}

static void test_capture_and_model(void)
{
    const LxpBackend *compositor = lxp_backend_find("compositor");
    g_autoptr(LxpTheme) theme = lxp_theme_new("Captured");
    g_autoptr(LxpTheme) plain = lxp_theme_new("Plain");
    g_autoptr(LxpPreviewModel) model = NULL;
    g_autoptr(LxpPreviewModel) defaults = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree char *radius = NULL;
    g_autofree char *backend = NULL;

    write_file(g_get_user_config_dir(), "picom.conf", picom_conf);
    g_assert_true(compositor->capture(theme, &error));
    g_assert_no_error(error);
    radius = lxp_theme_get_string(theme, "Compositor", "corner_radius");
    g_assert_cmpstr(radius, ==, "4");
    backend = lxp_theme_get_string(theme, "Compositor", "backend");
    g_assert_null(backend);  /* only themed settings are captured */

    model = lxp_preview_model_new(theme);
    g_assert_cmpint(model->corner_radius, ==, 4);
    g_assert_false(model->shadow);

    /* without compositor settings the preview uses picom's defaults */
    defaults = lxp_preview_model_new(plain);
    g_assert_cmpint(defaults->corner_radius, ==, 0);
    g_assert_false(defaults->shadow);
    g_assert_cmpint(defaults->shadow_radius, ==, 12);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func("/compositor/libconfig", test_libconfig);
    g_test_add_func("/compositor/apply", test_apply);
    g_test_add_func("/compositor/system-config", test_system_config);
    g_test_add_func("/compositor/check", test_check);
    g_test_add_func("/compositor/capture-and-model", test_capture_and_model);
    return g_test_run();
}
