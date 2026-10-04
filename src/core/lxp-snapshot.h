/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <glib.h>

/*
 * A snapshot is a copy of every file a theme apply may touch, stored in
 * ~/.local/state/lxqt-personalizer/snapshots/<id>/:
 *
 *   snapshot.ini   [Snapshot] Label, Created; [File N] Path, Present, Mode
 *   files/N        copy of file N (only when Present=true)
 *
 * Restoring puts every file back, and deletes files that did not exist
 * when the snapshot was taken.
 */

typedef struct {
    char      *id;
    char      *dir;
    char      *label;
    GDateTime *created;
    guint      n_files;
} LxpSnapshotInfo;

/*
 * Snapshot <files> (absolute paths), or every backend's managed files
 * when <files> is NULL. Returns the new snapshot id.
 */
char            *lxp_snapshot_create(const char *label, GPtrArray *files,
                                     GError **error);

/* Restore snapshot <id>, or the most recent one when <id> is NULL. */
gboolean         lxp_snapshot_restore(const char *id, GError **error);

/* LxpSnapshotInfo*, newest first. */
GPtrArray       *lxp_snapshot_list(void);
LxpSnapshotInfo *lxp_snapshot_info_load(const char *id, GError **error);
void             lxp_snapshot_info_free(LxpSnapshotInfo *info);

/* Delete all but the <keep> most recent snapshots. */
gboolean         lxp_snapshot_prune(guint keep, GError **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(LxpSnapshotInfo, lxp_snapshot_info_free)
