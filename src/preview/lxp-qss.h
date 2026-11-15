/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-preview-model.h"

/*
 * What the preview draws of an installed LXQt theme: the panel background
 * and text color and the look of task buttons, read from its
 * lxqt-panel.qss. Only plain rules are understood (the panel, its
 * #BackgroundWidget, #TaskBar QToolButton and its :on state): colors,
 * rgb()/rgba(), palette() roles of <palette> and the first and last stop of
 * a qlineargradient(), and the main colors of border and background images.
 * Anything else is ignored.
 */
typedef struct {
    gboolean          has_bg;
    LxpColor          bg, bg_to;   /* bg_to differs for a gradient */
    gboolean          has_text;
    LxpColor          text;
    gboolean          has_button_text;   /* buttons of every plugin */
    LxpColor          button_text;
    const char       *position;          /* the one asked for; not owned */
    LxpPanelTaskStyle task, task_active;
} LxpPanelQss;

/*
 * <position>: "Top", "Bottom"…; rules for other positions are skipped.
 * <dir>: where the theme's images are (NULL: images are ignored).
 */
void     lxp_panel_qss_parse(const char *qss, const char *dir, const char *position,
                             const LxpPreviewModel *palette, LxpPanelQss *out);
/* FALSE when <path> cannot be read. */
gboolean lxp_panel_qss_load(const char *path, const char *position,
                            const LxpPreviewModel *palette, LxpPanelQss *out);
