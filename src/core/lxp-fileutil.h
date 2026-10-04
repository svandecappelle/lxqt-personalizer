/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <glib.h>

/* Create the parent directory of <path> if needed. */
gboolean lxp_ensure_parent_dir(const char *path, GError **error);

/*
 * Atomically write <len> bytes to <path> (temp file + rename), creating
 * parent directories as needed.
 */
gboolean lxp_write_atomic(const char *path, const char *data, gssize len,
                          int mode, GError **error);

/* Atomically replace <dst> with a copy of <src>. */
gboolean lxp_copy_atomic(const char *src, const char *dst, int mode,
                         GError **error);

/* Recursively delete <path>. Missing paths are not an error. */
gboolean lxp_remove_tree(const char *path, GError **error);
