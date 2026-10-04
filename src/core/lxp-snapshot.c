/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-snapshot.h"

#include "lxp-backend.h"
#include "lxp-error.h"
#include "lxp-fileutil.h"
#include "lxp-paths.h"

#include <errno.h>
#include <glib/gstdio.h>

#define MANIFEST "snapshot.ini"
#define GROUP    "Snapshot"

static char *snapshot_dir(const char *id)
{
    g_autofree char *root = lxp_paths_snapshots_dir();
    return g_build_filename(root, id, NULL);
}

static char *file_group(guint index)
{
    return g_strdup_printf("File %u", index);
}

static char *stored_path(const char *dir, guint index)
{
    g_autofree char *name = g_strdup_printf("%u", index);
    return g_build_filename(dir, "files", name, NULL);
}

/* Sortable and unique: 20261004-215600-123456 */
static char *new_id(void)
{
    for (;;) {
        g_autoptr(GDateTime) now = g_date_time_new_now_local();
        g_autofree char *id = g_date_time_format(now, "%Y%m%d-%H%M%S-%f");
        g_autofree char *dir = snapshot_dir(id);

        if (!g_file_test(dir, G_FILE_TEST_EXISTS))
            return g_steal_pointer(&id);
        g_usleep(1);
    }
}

static gboolean snapshot_file(GKeyFile *kf, const char *dir, guint index,
                              const char *path, GError **error)
{
    g_autofree char *group = file_group(index);
    GStatBuf st;

    g_key_file_set_string(kf, group, "Path", path);

    if (g_stat(path, &st) != 0) {
        if (errno != ENOENT) {
            int err = errno;
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
                        "Failed to read “%s”: %s", path, g_strerror(err));
            return FALSE;
        }
        g_key_file_set_boolean(kf, group, "Present", FALSE);
        return TRUE;
    }

    {
        g_autofree char *dst = stored_path(dir, index);
        if (!lxp_copy_atomic(path, dst, 0600, error))
            return FALSE;
    }
    g_key_file_set_boolean(kf, group, "Present", TRUE);
    g_key_file_set_integer(kf, group, "Mode", st.st_mode & 07777);
    return TRUE;
}

char *lxp_snapshot_create(const char *label, GPtrArray *files, GError **error)
{
    g_autoptr(GPtrArray) owned = NULL;
    g_autoptr(GKeyFile) kf = g_key_file_new();
    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    g_autofree char *id = new_id();
    g_autofree char *dir = snapshot_dir(id);
    g_autofree char *manifest = g_build_filename(dir, MANIFEST, NULL);
    g_autofree char *created = g_date_time_format_iso8601(now);
    g_autofree char *data = NULL;

    if (!files)
        files = owned = lxp_backends_managed_files();

    g_key_file_set_string(kf, GROUP, "Label", label ? label : "");
    g_key_file_set_string(kf, GROUP, "Created", created);
    g_key_file_set_integer(kf, GROUP, "FileCount", (int)files->len);

    for (guint i = 0; i < files->len; i++) {
        if (!snapshot_file(kf, dir, i, g_ptr_array_index(files, i), error))
            goto fail;
    }

    /* Manifest last: its presence marks the snapshot as complete. */
    data = g_key_file_to_data(kf, NULL, NULL);
    if (!lxp_write_atomic(manifest, data, -1, 0600, error))
        goto fail;
    return g_steal_pointer(&id);

fail:
    lxp_remove_tree(dir, NULL);
    return NULL;
}

static char *latest_id(GError **error)
{
    g_autoptr(GPtrArray) list = lxp_snapshot_list();

    if (list->len == 0) {
        g_set_error_literal(error, LXP_ERROR, LXP_ERROR_NOT_FOUND,
                            "No snapshot available");
        return NULL;
    }
    return g_strdup(((LxpSnapshotInfo *)g_ptr_array_index(list, 0))->id);
}

gboolean lxp_snapshot_restore(const char *id, GError **error)
{
    g_autofree char *latest = NULL;
    g_autofree char *dir = NULL;
    g_autofree char *manifest = NULL;
    g_autoptr(GKeyFile) kf = g_key_file_new();
    int count;

    if (!id && !(id = latest = latest_id(error)))
        return FALSE;

    dir = snapshot_dir(id);
    manifest = g_build_filename(dir, MANIFEST, NULL);
    if (!g_key_file_load_from_file(kf, manifest, G_KEY_FILE_NONE, error))
        return FALSE;
    count = g_key_file_get_integer(kf, GROUP, "FileCount", NULL);

    /* Check everything first so a broken snapshot never half-restores. */
    for (int i = 0; i < count; i++) {
        g_autofree char *group = file_group(i);
        g_autofree char *path = g_key_file_get_string(kf, group, "Path", NULL);
        g_autofree char *src = stored_path(dir, i);

        if (!path || !g_path_is_absolute(path) ||
            (g_key_file_get_boolean(kf, group, "Present", NULL) &&
             !g_file_test(src, G_FILE_TEST_IS_REGULAR))) {
            g_set_error(error, LXP_ERROR, LXP_ERROR_INVALID,
                        "Snapshot %s is damaged (entry %d)", id, i);
            return FALSE;
        }
    }

    for (int i = 0; i < count; i++) {
        g_autofree char *group = file_group(i);
        g_autofree char *path = g_key_file_get_string(kf, group, "Path", NULL);

        if (g_key_file_get_boolean(kf, group, "Present", NULL)) {
            g_autofree char *src = stored_path(dir, i);
            int mode = g_key_file_get_integer(kf, group, "Mode", NULL);

            if (!lxp_copy_atomic(src, path, mode ? mode : 0644, error))
                return FALSE;
        } else if (g_unlink(path) != 0 && errno != ENOENT) {
            int err = errno;
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
                        "Failed to remove “%s”: %s", path, g_strerror(err));
            return FALSE;
        }
    }
    return TRUE;
}

LxpSnapshotInfo *lxp_snapshot_info_load(const char *id, GError **error)
{
    g_autofree char *dir = snapshot_dir(id);
    g_autofree char *manifest = g_build_filename(dir, MANIFEST, NULL);
    g_autoptr(GKeyFile) kf = g_key_file_new();
    g_autofree char *created = NULL;
    LxpSnapshotInfo *info;

    if (!g_key_file_load_from_file(kf, manifest, G_KEY_FILE_NONE, error))
        return NULL;

    info = g_new0(LxpSnapshotInfo, 1);
    info->id = g_strdup(id);
    info->dir = g_steal_pointer(&dir);
    info->label = g_key_file_get_string(kf, GROUP, "Label", NULL);
    created = g_key_file_get_string(kf, GROUP, "Created", NULL);
    if (created)
        info->created = g_date_time_new_from_iso8601(created, NULL);
    info->n_files = (guint)g_key_file_get_integer(kf, GROUP, "FileCount", NULL);
    return info;
}

void lxp_snapshot_info_free(LxpSnapshotInfo *info)
{
    if (!info)
        return;
    g_free(info->id);
    g_free(info->dir);
    g_free(info->label);
    g_clear_pointer(&info->created, g_date_time_unref);
    g_free(info);
}

static int compare_newest_first(gconstpointer a, gconstpointer b)
{
    const LxpSnapshotInfo *x = *(LxpSnapshotInfo *const *)a;
    const LxpSnapshotInfo *y = *(LxpSnapshotInfo *const *)b;

    return g_strcmp0(y->id, x->id);
}

GPtrArray *lxp_snapshot_list(void)
{
    g_autofree char *root = lxp_paths_snapshots_dir();
    g_autoptr(GDir) dir = g_dir_open(root, 0, NULL);
    GPtrArray *list = g_ptr_array_new_with_free_func(
        (GDestroyNotify)lxp_snapshot_info_free);
    const char *name;

    if (!dir)
        return list;
    while ((name = g_dir_read_name(dir))) {
        LxpSnapshotInfo *info = lxp_snapshot_info_load(name, NULL);
        if (info)
            g_ptr_array_add(list, info);
    }
    g_ptr_array_sort(list, compare_newest_first);
    return list;
}

gboolean lxp_snapshot_prune(guint keep, GError **error)
{
    g_autoptr(GPtrArray) list = lxp_snapshot_list();

    for (guint i = keep; i < list->len; i++) {
        LxpSnapshotInfo *info = g_ptr_array_index(list, i);
        if (!lxp_remove_tree(info->dir, error))
            return FALSE;
    }
    return TRUE;
}
