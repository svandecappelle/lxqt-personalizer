/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gtk/gtk.h>

/* Called with the directory of the saved theme. */
typedef void (*LxpEditorSaved)(const char *dir, gpointer user_data);

/*
 * Open the theme editor: pick a few colors, a font, icons, wallpaper and
 * panel options with a live preview, then generate a complete theme.
 * <dir> is an installed theme to edit, or NULL to create a new theme
 * starting from the current desktop.
 */
void lxp_editor_open(GtkWindow *parent, const char *dir, LxpEditorSaved on_saved,
                     gpointer user_data);
