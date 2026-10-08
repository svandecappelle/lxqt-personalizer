/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-fileutil.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <sys/stat.h>

static gboolean set_errno_error(GError **error, int err, const char *what,
                                const char *path)
{
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
                "Failed to %s “%s”: %s", what, path, g_strerror(err));
    return FALSE;
}

gboolean lxp_ensure_parent_dir(const char *path, GError **error)
{
    g_autofree char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0755) != 0)
        return set_errno_error(error, errno, "create directory", dir);
    return TRUE;
}

gboolean lxp_write_atomic(const char *path, const char *data, gssize len,
                          int mode, GError **error)
{
    if (!lxp_ensure_parent_dir(path, error))
        return FALSE;
    return g_file_set_contents_full(path, data, len,
                                    G_FILE_SET_CONTENTS_CONSISTENT,
                                    mode, error);
}

gboolean lxp_copy_atomic(const char *src, const char *dst, int mode,
                         GError **error)
{
    g_autofree char *data = NULL;
    gsize len;

    if (!g_file_get_contents(src, &data, &len, error))
        return FALSE;
    return lxp_write_atomic(dst, data, (gssize)len, mode, error);
}

gboolean lxp_copy_tree(const char *src, const char *dst, GError **error)
{
    g_autoptr(GDir) dir = NULL;
    const char *name;
    GStatBuf st;

    if (g_lstat(src, &st) != 0)
        return set_errno_error(error, errno, "read", src);
    if (!S_ISDIR(st.st_mode))
        return lxp_copy_atomic(src, dst, st.st_mode & 0777, error);

    if (g_mkdir_with_parents(dst, 0755) != 0)
        return set_errno_error(error, errno, "create directory", dst);
    if (!(dir = g_dir_open(src, 0, error)))
        return FALSE;
    while ((name = g_dir_read_name(dir))) {
        g_autofree char *s = g_build_filename(src, name, NULL);
        g_autofree char *d = g_build_filename(dst, name, NULL);

        if (g_file_test(s, G_FILE_TEST_IS_SYMLINK))
            continue;  /* never follow links out of a theme */
        if (!lxp_copy_tree(s, d, error))
            return FALSE;
    }
    return TRUE;
}

gboolean lxp_remove_tree(const char *path, GError **error)
{
    if (!g_file_test(path, G_FILE_TEST_EXISTS))
        return TRUE;

    if (g_file_test(path, G_FILE_TEST_IS_DIR) &&
        !g_file_test(path, G_FILE_TEST_IS_SYMLINK)) {
        g_autoptr(GDir) dir = g_dir_open(path, 0, error);
        const char *name;

        if (!dir)
            return FALSE;
        while ((name = g_dir_read_name(dir))) {
            g_autofree char *child = g_build_filename(path, name, NULL);
            if (!lxp_remove_tree(child, error))
                return FALSE;
        }
        if (g_rmdir(path) != 0)
            return set_errno_error(error, errno, "remove", path);
        return TRUE;
    }

    if (g_unlink(path) != 0)
        return set_errno_error(error, errno, "remove", path);
    return TRUE;
}
