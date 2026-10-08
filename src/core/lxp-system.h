/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <glib.h>

/* Run <argv> synchronously; fails on spawn error or non-zero exit. */
gboolean lxp_run(const char *const *argv, GError **error);

/*
 * Command line of the first process of the current user whose executable
 * name is <name>, or NULL if none is running.
 */
GStrv    lxp_process_find(const char *name);

/* PID of that process, or 0 if none is running. */
GPid     lxp_process_pid(const char *name);

/* Value of "--<option>=VALUE" or "--<option> VALUE" in <argv>, or NULL. */
char    *lxp_argv_option(const char *const *argv, const char *option);

/*
 * Find an installed asset: the first directory <dir>/<name>/<probe> exists
 * for <dir> in $XDG_DATA_HOME/<kind>, ~/.<kind> (themes and icons only),
 * then $XDG_DATA_DIRS/<kind>. <probe> may be NULL. Returns the
 * <dir>/<name> path or NULL.
 */
char    *lxp_find_installed(const char *kind, const char *name,
                            const char *probe);
