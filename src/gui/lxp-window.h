/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gtk/gtk.h>

#define LXP_TYPE_WINDOW (lxp_window_get_type())
G_DECLARE_FINAL_TYPE(LxpWindow, lxp_window, LXP, WINDOW, GtkApplicationWindow)

LxpWindow *lxp_window_new(GtkApplication *app);
