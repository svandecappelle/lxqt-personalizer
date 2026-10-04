/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-error.h"
#include "lxp-snapshot.h"
#include "test-util.h"

#include <glib/gstdio.h>

static char *slurp(const char *path)
{
    char *data = NULL;
    g_file_get_contents(path, &data, NULL, NULL);
    return data;
}

static void test_create_restore(void)
{
    g_autofree char *a = g_build_filename(g_get_user_config_dir(), "a.conf", NULL);
    g_autofree char *b = g_build_filename(g_get_user_config_dir(), "sub", "b.conf", NULL);
    g_autoptr(GPtrArray) files = g_ptr_array_new();
    g_autoptr(GError) error = NULL;
    g_autofree char *id = NULL;
    g_autofree char *content = NULL;
    GStatBuf st;

    g_assert_true(lxp_write_atomic(a, "original", -1, 0640, NULL));
    g_ptr_array_add(files, a);
    g_ptr_array_add(files, b); /* does not exist yet */

    id = lxp_snapshot_create("before apply", files, &error);
    g_assert_no_error(error);
    g_assert_nonnull(id);

    /* simulate a theme apply */
    g_assert_true(lxp_write_atomic(a, "changed", -1, 0644, NULL));
    g_assert_true(lxp_write_atomic(b, "new file", -1, 0644, NULL));

    g_assert_true(lxp_snapshot_restore(NULL, &error));
    g_assert_no_error(error);

    content = slurp(a);
    g_assert_cmpstr(content, ==, "original");
    g_assert_cmpint(g_stat(a, &st), ==, 0);
    g_assert_cmpint(st.st_mode & 07777, ==, 0640);
    g_assert_false(g_file_test(b, G_FILE_TEST_EXISTS));
}

static void test_list_prune(void)
{
    g_autoptr(GPtrArray) files = g_ptr_array_new();
    g_autoptr(GPtrArray) list = NULL;
    g_autoptr(GError) error = NULL;
    char *ids[3];

    for (int i = 0; i < 3; i++) {
        g_autofree char *label = g_strdup_printf("snap %d", i);
        ids[i] = lxp_snapshot_create(label, files, &error);
        g_assert_no_error(error);
    }

    list = lxp_snapshot_list();
    g_assert_cmpuint(list->len, ==, 3);
    g_assert_cmpstr(((LxpSnapshotInfo *)list->pdata[0])->id, ==, ids[2]);
    g_assert_cmpstr(((LxpSnapshotInfo *)list->pdata[0])->label, ==, "snap 2");
    g_assert_nonnull(((LxpSnapshotInfo *)list->pdata[0])->created);
    g_clear_pointer(&list, g_ptr_array_unref);

    g_assert_true(lxp_snapshot_prune(1, &error));
    list = lxp_snapshot_list();
    g_assert_cmpuint(list->len, ==, 1);
    g_assert_cmpstr(((LxpSnapshotInfo *)list->pdata[0])->id, ==, ids[2]);

    for (int i = 0; i < 3; i++)
        g_free(ids[i]);
}

static void test_restore_errors(void)
{
    g_autofree char *a = g_build_filename(g_get_user_config_dir(), "a.conf", NULL);
    g_autoptr(GPtrArray) files = g_ptr_array_new();
    g_autoptr(GError) error = NULL;
    g_autofree char *id = NULL;
    g_autofree char *stored = NULL;
    g_autoptr(LxpSnapshotInfo) info = NULL;

    g_assert_false(lxp_snapshot_restore(NULL, &error));
    g_assert_error(error, LXP_ERROR, LXP_ERROR_NOT_FOUND);
    g_clear_error(&error);

    g_assert_true(lxp_write_atomic(a, "original", -1, 0644, NULL));
    g_ptr_array_add(files, a);
    id = lxp_snapshot_create(NULL, files, &error);
    g_assert_no_error(error);

    /* damage the snapshot: restore must refuse and leave files untouched */
    info = lxp_snapshot_info_load(id, &error);
    g_assert_no_error(error);
    stored = g_build_filename(info->dir, "files", "0", NULL);
    g_assert_cmpint(g_unlink(stored), ==, 0);
    g_assert_true(lxp_write_atomic(a, "changed", -1, 0644, NULL));

    g_assert_false(lxp_snapshot_restore(id, &error));
    g_assert_error(error, LXP_ERROR, LXP_ERROR_INVALID);
    {
        g_autofree char *content = slurp(a);
        g_assert_cmpstr(content, ==, "changed");
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func("/snapshot/create-restore", test_create_restore);
    g_test_add_func("/snapshot/list-prune", test_list_prune);
    g_test_add_func("/snapshot/restore-errors", test_restore_errors);
    return g_test_run();
}
