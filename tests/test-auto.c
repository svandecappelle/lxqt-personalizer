/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-auto.h"
#include "lxp-error.h"
#include "test-util.h"

static void test_times(void)
{
    int m = -1;

    g_assert_true(lxp_auto_parse_time("7:30", &m));
    g_assert_cmpint(m, ==, 450);
    g_assert_true(lxp_auto_parse_time("23:59", &m));
    g_assert_cmpint(m, ==, 1439);
    g_assert_true(lxp_auto_parse_time("00:00", &m));
    g_assert_cmpint(m, ==, 0);
    g_assert_false(lxp_auto_parse_time("24:00", &m));
    g_assert_false(lxp_auto_parse_time("7:60", &m));
    g_assert_false(lxp_auto_parse_time("7", &m));
    g_assert_false(lxp_auto_parse_time("7:5", &m));
    g_assert_false(lxp_auto_parse_time("-7:30", &m));
    g_assert_false(lxp_auto_parse_time("07:30:00", &m));
    g_assert_false(lxp_auto_parse_time("", &m));
    g_assert_false(lxp_auto_parse_time(NULL, &m));
    {
        g_autofree char *text = lxp_auto_format_time(450);
        g_assert_cmpstr(text, ==, "07:30");
    }
}

static void test_wants_dark(void)
{
    LxpAutoConfig day = { (char *)"x", 7 * 60, 19 * 60 };
    /* dark only in the small hours: light from 05:00 through midnight to 01:00 */
    LxpAutoConfig late = { (char *)"x", 5 * 60, 1 * 60 };

    g_assert_true(lxp_auto_wants_dark(&day, 0));
    g_assert_true(lxp_auto_wants_dark(&day, 7 * 60 - 1));
    g_assert_false(lxp_auto_wants_dark(&day, 7 * 60));
    g_assert_false(lxp_auto_wants_dark(&day, 19 * 60 - 1));
    g_assert_true(lxp_auto_wants_dark(&day, 19 * 60));
    g_assert_true(lxp_auto_wants_dark(&day, 23 * 60 + 59));

    g_assert_false(lxp_auto_wants_dark(&late, 0));
    g_assert_true(lxp_auto_wants_dark(&late, 60));
    g_assert_true(lxp_auto_wants_dark(&late, 5 * 60 - 1));
    g_assert_false(lxp_auto_wants_dark(&late, 5 * 60));
    g_assert_false(lxp_auto_wants_dark(&late, 23 * 60));
}

static void test_config(void)
{
    LxpAutoConfig config = { (char *)"nord", 6 * 60 + 15, 20 * 60 };
    LxpAutoConfig same = { (char *)"nord", 600, 600 };
    g_autoptr(LxpAutoConfig) loaded = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree char *units = lxp_auto_units_dir();
    g_autofree char *service = NULL;
    g_autofree char *timer = NULL;

    g_assert_null(lxp_auto_load(&error));
    g_assert_error(error, LXP_ERROR, LXP_ERROR_NOT_FOUND);
    g_clear_error(&error);

    g_assert_false(lxp_auto_save(&same, &error));
    g_assert_error(error, LXP_ERROR, LXP_ERROR_INVALID);
    g_clear_error(&error);

    g_assert_true(lxp_auto_save(&config, &error));
    g_assert_no_error(error);
    loaded = lxp_auto_load(&error);
    g_assert_no_error(error);
    g_assert_cmpstr(loaded->theme, ==, "nord");
    g_assert_cmpint(loaded->light, ==, 375);
    g_assert_cmpint(loaded->dark, ==, 1200);

    g_assert_false(lxp_auto_write_units(&config, "lxp", &error));
    g_assert_error(error, LXP_ERROR, LXP_ERROR_INVALID);
    g_clear_error(&error);
    g_assert_true(lxp_auto_write_units(&config, "/opt/my apps/100%/lxp", &error));
    g_assert_no_error(error);
    service = read_file(units, LXP_AUTO_SERVICE);
    timer = read_file(units, LXP_AUTO_TIMER);
    g_assert_nonnull(strstr(service, "ExecStart=\"/opt/my apps/100%%/lxp\" auto run\n"));
    g_assert_nonnull(strstr(timer, "OnCalendar=*-*-* 06:15:00\n"));
    g_assert_nonnull(strstr(timer, "OnCalendar=*-*-* 20:00:00\n"));
    g_assert_nonnull(strstr(timer, "Persistent=true\n"));

    g_assert_null(lxp_auto_get_state());
    g_assert_false(lxp_auto_state_matches("nord", TRUE));
    g_assert_true(lxp_auto_set_state("nord", TRUE, &error));
    g_assert_no_error(error);
    g_assert_true(lxp_auto_state_matches("nord", TRUE));
    g_assert_false(lxp_auto_state_matches("nord", FALSE));
    g_assert_false(lxp_auto_state_matches("nor", TRUE));

    g_assert_true(lxp_auto_remove(&error));
    g_assert_no_error(error);
    g_assert_null(read_file(units, LXP_AUTO_TIMER));
    g_assert_null(read_file(units, LXP_AUTO_SERVICE));
    g_assert_null(lxp_auto_get_state());
    g_assert_null(lxp_auto_load(NULL));
    /* nothing left to remove is not an error */
    g_assert_true(lxp_auto_remove(&error));
    g_assert_no_error(error);
}

static void test_bad_config(void)
{
    g_autoptr(GError) error = NULL;

    write_file(g_get_user_config_dir(), "lxqt-personalizer/auto.conf",
               "[Auto]\ntheme=nord\nlight=7h\n");
    g_assert_null(lxp_auto_load(&error));
    g_assert_error(error, LXP_ERROR, LXP_ERROR_INVALID);
    g_clear_error(&error);

    write_file(g_get_user_config_dir(), "lxqt-personalizer/auto.conf", "[Auto]\nlight=07:00\n");
    g_assert_null(lxp_auto_load(&error));
    g_assert_error(error, LXP_ERROR, LXP_ERROR_INVALID);
    g_clear_error(&error);

    /* times are optional */
    write_file(g_get_user_config_dir(), "lxqt-personalizer/auto.conf", "[Auto]\ntheme=nord\n");
    {
        g_autoptr(LxpAutoConfig) config = lxp_auto_load(&error);
        g_assert_no_error(error);
        g_assert_cmpint(config->light, ==, LXP_AUTO_DEFAULT_LIGHT);
        g_assert_cmpint(config->dark, ==, LXP_AUTO_DEFAULT_DARK);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func("/auto/times", test_times);
    g_test_add_func("/auto/wants-dark", test_wants_dark);
    g_test_add_func("/auto/config", test_config);
    g_test_add_func("/auto/bad-config", test_bad_config);
    return g_test_run();
}
