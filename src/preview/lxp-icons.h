/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <glib.h>

/*
 * Freedesktop icon theme lookup without GTK: path of the first of <names>
 * found in icon theme <theme>, the themes it inherits, or hicolor, in the
 * directory closest to <size>. As in Qt, every name is searched through
 * the whole chain before the next one is tried. NULL if none exists.
 * Results are cached; main thread only.
 */
const char *lxp_icon_lookup(const char *theme, const char *const *names, int size);

/* Names of installed icon themes (not cursor-only ones), sorted. */
GPtrArray  *lxp_icon_themes_list(void);
