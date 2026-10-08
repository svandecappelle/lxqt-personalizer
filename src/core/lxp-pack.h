/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-theme.h"

/*
 * Theme packs: a .tar.gz with one top-level directory holding theme.ini,
 * its wallpaper, its generated data/ and, optionally, preview.png.
 *
 * Export works on a copy, so the installed theme is never modified: the
 * wallpaper is copied in, and Openbox/GTK/LXQt themes installed in the
 * user's home are bundled (installed on the other side only when missing).
 * System-wide themes are not bundled: packages provide them.
 *
 * Import treats the archive as untrusted: regular files and directories
 * only, no absolute paths, no "..", size limits, files 0644/dirs 0755.
 */

typedef enum {
    LXP_PACK_DEFAULT        = 0,
    LXP_PACK_NO_BUNDLE      = 1 << 0,  /* export: do not bundle used themes */
    LXP_PACK_FORCE          = 1 << 1,  /* import: replace an existing theme */
} LxpPackFlags;

/*
 * Write <theme> to <archive>. <preview_png> (may be NULL) is added as
 * preview.png. Notes about skipped assets are appended to <notes>
 * (char *, may be NULL).
 */
gboolean lxp_pack_export(const LxpTheme *theme, const char *archive,
                         const char *preview_png, LxpPackFlags flags,
                         GPtrArray *notes, GError **error);

/* Install the theme in <archive>; returns its new directory. */
char    *lxp_pack_import(const char *archive, LxpPackFlags flags, GError **error);
