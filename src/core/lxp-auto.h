/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <glib.h>

/*
 * Automatic light/dark switching: a theme with a light/dark variant is
 * applied in its light version from one time of day and in its dark
 * version from another. The schedule is stored in
 * ~/.config/lxqt-personalizer/auto.conf:
 *
 *   [Auto]
 *   theme=nord      theme name or directory
 *   light=07:30
 *   dark=19:00
 *
 * A systemd user timer (lxp-auto.timer) runs "lxp auto run" at both times,
 * and at login when one was missed. What was applied last is remembered in
 * ~/.local/state/lxqt-personalizer/auto-state, so a run between two
 * switches changes nothing: a theme chosen by hand stays until the next
 * switch.
 */

#define LXP_AUTO_SERVICE "lxp-auto.service"
#define LXP_AUTO_TIMER   "lxp-auto.timer"

#define LXP_AUTO_DEFAULT_LIGHT (7 * 60)
#define LXP_AUTO_DEFAULT_DARK  (19 * 60)

typedef struct {
    char *theme;
    int   light;   /* minutes after midnight */
    int   dark;
} LxpAutoConfig;

/* "7:30" or "07:30" -> 450. */
gboolean       lxp_auto_parse_time(const char *text, int *minutes);
/* 450 -> "07:30" */
char          *lxp_auto_format_time(int minutes);

/* LXP_ERROR_NOT_FOUND when automatic switching is not set up. */
LxpAutoConfig *lxp_auto_load(GError **error);
gboolean       lxp_auto_save(const LxpAutoConfig *config, GError **error);
void           lxp_auto_config_free(LxpAutoConfig *config);

/* Is the dark version due at <minute> (minutes after midnight)? */
gboolean       lxp_auto_wants_dark(const LxpAutoConfig *config, int minute);

/* ~/.config/systemd/user */
char          *lxp_auto_units_dir(void);
/* Write the service and timer units; <lxp> is the absolute path of lxp. */
gboolean       lxp_auto_write_units(const LxpAutoConfig *config, const char *lxp,
                                    GError **error);

/* What the last run applied ("<theme> light|dark"), or NULL. */
char          *lxp_auto_get_state(void);
gboolean       lxp_auto_set_state(const char *theme, gboolean dark, GError **error);
/* Does the state say that this version of <theme> was applied last? */
gboolean       lxp_auto_state_matches(const char *theme, gboolean dark);

/* Delete the schedule, the units and the state. */
gboolean       lxp_auto_remove(GError **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(LxpAutoConfig, lxp_auto_config_free)
