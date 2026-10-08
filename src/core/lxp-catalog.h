/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <glib.h>

/*
 * What is installed and can be chosen in a theme. Generated themes of
 * other lxqt-personalizer themes ("lxp-*") are left out.
 */
typedef enum {
    LXP_CATALOG_OPENBOX,     /* themes/X/openbox-3/themerc */
    LXP_CATALOG_GTK,         /* themes/X/gtk-3.0, plus GTK's built-in themes */
    LXP_CATALOG_LXQT,        /* lxqt/themes/X */
    LXP_CATALOG_CURSORS,     /* icons/X/cursors */
    LXP_CATALOG_QT_STYLES,   /* Fusion, Windows and installed style plugins */
} LxpCatalogKind;

/* Sorted, unique names (case-insensitive order). */
GPtrArray *lxp_catalog_list(LxpCatalogKind kind);
