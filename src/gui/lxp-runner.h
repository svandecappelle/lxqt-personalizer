/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <gio/gio.h>

/*
 * Runs the lxp command-line tool for operations that change the desktop
 * (apply, rollback, reload). Running them out of process means a "try"
 * still reverts if the GUI crashes: lxp sees its stdin close.
 */

typedef struct _LxpRunner LxpRunner;

typedef void (*LxpRunnerEvent)(LxpRunner *runner, const char *event,
                               const char *arg, gpointer user_data);
/* <error> is NULL on success; <warnings> is stderr output, possibly empty. */
typedef void (*LxpRunnerDone)(LxpRunner *runner, const GError *error,
                              const char *warnings, gpointer user_data);

/* Path of the lxp binary: $LXP_CLI, next to this program, or $PATH. */
char      *lxp_runner_find_cli(void);

/*
 * Start "lxp <args>". <on_event> receives porcelain lines split into
 * event and argument ("confirm", "15"). <on_done> is called once.
 */
LxpRunner *lxp_runner_start(const char *const *args, LxpRunnerEvent on_event,
                            LxpRunnerDone on_done, gpointer user_data,
                            GError **error);

/* Write one line to the process (e.g. "y" to keep a tried theme). */
void       lxp_runner_send(LxpRunner *runner, const char *line);
