/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-color.h"

/*
 * A small icon theme <name> in <dir> that inherits installed icon theme
 * <base> and replaces its folder icons (places: folder*, user-home,
 * user-desktop, inode-directory) with copies recolored to <accent>.
 * SVG icons only. Fails with LXP_ERROR_NOT_FOUND when <base> is not
 * installed or has no SVG folder icons.
 */
gboolean lxp_icons_folder_overlay(const char *base, const LxpColor *accent, const char *name,
                                  const char *dir, GError **error);
