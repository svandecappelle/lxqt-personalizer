/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <glib.h>

/*
 * Format-preserving editor for top-level scalar settings of libconfig
 * files such as picom.conf:
 *
 *   corner-radius = 10;      # comments, lists and groups are kept as is
 *   shadow-exclude = [ "window_type = 'dock'" ];
 *
 * Only top-level "name = value;" (or "name: value") settings are read or
 * written, whole; values inside groups and lists are never edited.
 */

typedef struct _LxpLibconfig LxpLibconfig;

LxpLibconfig *lxp_libconfig_new_from_data(const char *data);
LxpLibconfig *lxp_libconfig_load(const char *path, gboolean missing_ok, GError **error);
gboolean      lxp_libconfig_save(const LxpLibconfig *conf, const char *path, GError **error);
void          lxp_libconfig_free(LxpLibconfig *conf);
const char   *lxp_libconfig_to_data(const LxpLibconfig *conf);

/* Raw value text of top-level setting <name> (e.g. "10", "true", "0.4"). */
char         *lxp_libconfig_get(const LxpLibconfig *conf, const char *name);
/* Replace the value in place, or append "name = value;". */
void          lxp_libconfig_set(LxpLibconfig *conf, const char *name, const char *value);
/* Same for a structured value (list, group) built by the caller: no sanitizing. */
void          lxp_libconfig_set_raw(LxpLibconfig *conf, const char *name, const char *value);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(LxpLibconfig, lxp_libconfig_free)
