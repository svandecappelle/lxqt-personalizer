/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <glib.h>

/*
 * Format-preserving INI editor for files owned by other programs
 * (QSettings files, GTK settings.ini, gtkrc-2.0, index.theme).
 *
 * Unlike GKeyFile, untouched lines (comments, spacing, unknown keys,
 * Qt "@Variant(...)" values) are written back byte for byte. Values are
 * raw strings; quoting is up to the caller. Keys before the first
 * [group] header belong to the NULL group (used by gtkrc-2.0).
 */

typedef struct _LxpIni LxpIni;

LxpIni      *lxp_ini_new(void);
LxpIni      *lxp_ini_new_from_data(const char *data);
/* A missing file yields an empty document when <missing_ok>. */
LxpIni      *lxp_ini_load(const char *path, gboolean missing_ok, GError **error);
/* Atomic write; keeps the existing file mode, or uses 0644. */
gboolean     lxp_ini_save(const LxpIni *ini, const char *path, GError **error);
void         lxp_ini_free(LxpIni *ini);
char        *lxp_ini_to_data(const LxpIni *ini);

/* Raw value with surrounding whitespace removed, or NULL. */
const char  *lxp_ini_get(const LxpIni *ini, const char *group, const char *key);
/* Replace the value in place, or append the key to its group. */
void         lxp_ini_set(LxpIni *ini, const char *group, const char *key,
                         const char *value);
void         lxp_ini_remove(LxpIni *ini, const char *group, const char *key);
gboolean     lxp_ini_has_group(const LxpIni *ini, const char *group);
void         lxp_ini_remove_group(LxpIni *ini, const char *group);
/* Group names (without NULL), in file order. */
GStrv        lxp_ini_get_groups(const LxpIni *ini);
/* Keys of <group>, in file order. */
GStrv        lxp_ini_get_keys(const LxpIni *ini, const char *group);

/* QSettings value encoding for plain strings. */
char        *lxp_qt_quote(const char *value);
char        *lxp_qt_unquote(const char *raw);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(LxpIni, lxp_ini_free)
