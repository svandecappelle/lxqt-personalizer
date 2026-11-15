/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-apply.h"
#include "lxp-color.h"
#include "lxp-error.h"
#include "lxp-scheme.h"
#include "lxp-theme.h"
#include "lxp-themegen.h"
#include "test-util.h"

#define SHIPPED_DIR TEST_DATA_DIR "/../../data/themes"

/* Every theme shipped in data/themes is readable, legible and generates. */
static void test_shipped(void)
{
    g_autoptr(GPtrArray) list = NULL;

    g_setenv("LXP_BUILTIN_DIR", SHIPPED_DIR, TRUE);
    list = lxp_theme_list_builtin();
    g_assert_cmpuint(list->len, >=, 8);

    for (guint i = 0; i < list->len; i++) {
        const char *source = g_ptr_array_index(list, i);
        g_autofree char *slug = g_path_get_basename(source);
        g_autoptr(GError) error = NULL;
        g_autoptr(LxpTheme) theme = lxp_theme_load(source, &error);
        g_autofree char *usable = NULL;
        g_autofree char *own_slug = NULL;
        g_autofree char *wants_variant = NULL;
        g_autofree char *variant = NULL;
        g_autoptr(LxpTheme) generated = NULL;
        LxpScheme s;
        LxpSchemeColors c;

        g_test_message("built-in theme %s", slug);
        g_assert_no_error(error);
        /* "lxp apply <folder name>" and the theme's name agree */
        own_slug = lxp_theme_slugify(lxp_theme_get_name(theme));
        g_assert_cmpstr(own_slug, ==, slug);

        g_assert_true(lxp_scheme_load(theme, &s, &error));
        g_assert_no_error(error);
        lxp_scheme_derive(&s, &c);
        lxp_scheme_clear(&s);
        g_assert_cmpfloat(lxp_color_contrast(&c.fg, &c.bg), >=, 7);
        g_assert_cmpfloat(lxp_color_contrast(&c.fg, &c.surface), >=, 7);
        g_assert_cmpfloat(lxp_color_contrast(&c.accent_fg, &c.accent), >=, 4.5);
        g_assert_cmpfloat(lxp_color_contrast(&c.title_active_fg, &c.title_active), >=, 4.5);
        g_assert_cmpfloat(lxp_color_contrast(&c.panel_fg, &c.panel), >=, 4.5);

        usable = lxp_theme_materialize(source, &error);
        g_assert_no_error(error);
        generated = lxp_theme_load(usable, &error);
        g_assert_no_error(error);
        /* every value is accepted by the backends that are installed */
        {
            g_autoptr(GPtrArray) problems = lxp_check_theme_all(generated, NULL);
            for (guint p = 0; p < problems->len; p++) {
                const GError *problem = g_ptr_array_index(problems, p);
                g_assert_false(g_error_matches(problem, LXP_ERROR, LXP_ERROR_INVALID));
            }
        }

        wants_variant = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "variant");
        variant = lxp_theme_variant_dir(usable);
        if (g_strcmp0(wants_variant, "true") == 0)
            g_assert_nonnull(variant);
        else
            g_assert_null(variant);
    }
    g_unsetenv("LXP_BUILTIN_DIR");
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func("/builtin/shipped", test_shipped);
    return g_test_run();
}
