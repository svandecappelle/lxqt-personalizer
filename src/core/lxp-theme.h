/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <glib.h>

/*
 * A theme is a directory containing a "theme.ini" manifest plus optional
 * assets (wallpaper, preview.png, generated files...).
 *
 *   [Theme]
 *   Name=Nord Night
 *   FormatVersion=1
 *
 *   [LXQt]           <- one or more groups per backend
 *   theme=nord
 */

#define LXP_THEME_MANIFEST       "theme.ini"
#define LXP_THEME_GROUP          "Theme"
#define LXP_THEME_FORMAT_VERSION 1

typedef struct _LxpTheme LxpTheme;

LxpTheme   *lxp_theme_new(const char *name);
LxpTheme   *lxp_theme_load(const char *dir, GError **error);
gboolean    lxp_theme_save(LxpTheme *theme, const char *dir, GError **error);
void        lxp_theme_free(LxpTheme *theme);

const char *lxp_theme_get_name(const LxpTheme *theme);
/* Directory the theme was loaded from / saved to, or NULL. */
const char *lxp_theme_get_dir(const LxpTheme *theme);
/* Underlying key file; owned by the theme. */
GKeyFile   *lxp_theme_get_keyfile(const LxpTheme *theme);

/* Returns NULL if the key is missing. */
char       *lxp_theme_get_string(const LxpTheme *theme, const char *group,
                                 const char *key);
void        lxp_theme_set_string(LxpTheme *theme, const char *group,
                                 const char *key, const char *value);
char      **lxp_theme_get_string_list(const LxpTheme *theme, const char *group,
                                      const char *key);
void        lxp_theme_set_string_list(LxpTheme *theme, const char *group,
                                      const char *key,
                                      const char *const *values);
void        lxp_theme_remove_group(LxpTheme *theme, const char *group);
char       *lxp_theme_to_data(const LxpTheme *theme);

/*
 * Make the theme self-contained: copy an absolute [Wallpaper] file, and
 * an absolute [Panel] menu_icon, that live outside <dir> into <dir> as
 * "wallpaper.<ext>" and "menu-icon.<ext>" and refer to them by these
 * relative names.
 */
gboolean    lxp_theme_bundle_wallpaper(LxpTheme *theme, const char *dir, GError **error);

/* "Nord Night!" -> "nord-night" */
char       *lxp_theme_slugify(const char *name);

/*
 * Resolve <name_or_path>: an existing directory containing theme.ini, or
 * the slug of a theme in the user themes directory, else of a built-in.
 */
char       *lxp_theme_resolve(const char *name_or_path, GError **error);

/* Sorted directory names of installed themes. */
GPtrArray  *lxp_theme_list_installed(void);

/* Directories of built-in themes, sorted by name. */
GPtrArray  *lxp_theme_list_builtin(void);

/* Is <dir> a built-in theme (read-only, shipped with the program)? */
gboolean    lxp_theme_is_builtin(const char *dir);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(LxpTheme, lxp_theme_free)
