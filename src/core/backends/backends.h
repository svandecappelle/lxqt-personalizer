/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-backend.h"

extern const LxpBackend lxp_backend_lxqt;
extern const LxpBackend lxp_backend_openbox;
extern const LxpBackend lxp_backend_gtk;
extern const LxpBackend lxp_backend_cursor;
extern const LxpBackend lxp_backend_wallpaper;
extern const LxpBackend lxp_backend_panel;
extern const LxpBackend lxp_backend_compositor;
extern const LxpBackend lxp_backend_kvantum;
extern const LxpBackend lxp_backend_terminal;

/* GTK settings shared by the gtk and cursor backends (gtk.c). */
typedef struct {
    const char *key;       /* "gtk-theme-name" */
    const char *value;     /* NULL: leave untouched */
    gboolean    is_string; /* quoted in gtkrc-2.0 */
    gboolean    gtk4;      /* also written to gtk-4.0/settings.ini */
} LxpGtkSetting;

GPtrArray *lxp_gtk_settings_files(void);
gboolean   lxp_gtk_write_settings(const LxpGtkSetting *settings, gsize n,
                                  GError **error);

/* Set org.gnome.desktop.interface keys when the schema is installed. */
void       lxp_gsettings_set(const char *key, const char *value);
void       lxp_gsettings_set_int(const char *key, int value);
void       lxp_gsettings_sync(void);
