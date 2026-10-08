/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <glib.h>

/* All functions return newly allocated strings (free with g_free). */

/* ~/.local/share/lxqt-personalizer/themes */
char *lxp_paths_themes_dir(void);

/*
 * Directories holding built-in themes: $XDG_DATA_DIRS/lxqt-personalizer/themes
 * and the install prefix's, or only $LXP_BUILTIN_DIR when it is set
 * (development, tests). Only existing ones.
 */
GPtrArray *lxp_paths_builtin_dirs(void);

/* ~/.cache/lxqt-personalizer/builtin */
char *lxp_paths_builtin_cache_dir(void);

/* ~/.local/state/lxqt-personalizer/snapshots */
char *lxp_paths_snapshots_dir(void);

/* $XDG_CONFIG_HOME/<relpath> */
char *lxp_paths_user_config(const char *relpath);

/*
 * First existing file named <relpath> in $XDG_CONFIG_HOME, then
 * $XDG_CONFIG_DIRS, then $XDG_DATA_DIRS. NULL if none exists.
 */
char *lxp_paths_find_config(const char *relpath);

/*
 * Every existing file named <relpath>, lowest priority first
 * ($XDG_DATA_DIRS reversed, then $XDG_CONFIG_DIRS reversed, then
 * $XDG_CONFIG_HOME). Suitable for layered loading.
 */
GPtrArray *lxp_paths_find_config_layers(const char *relpath);
