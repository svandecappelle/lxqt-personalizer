/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-scheme.h"

/*
 * A Kvantum theme drawn from a scheme: rounded buttons, fields, menus,
 * selections, tabs, scrollbars, sliders and indicators in the scheme's
 * colors, with corners of <radius> pixels (0 for square).
 *
 * Kvantum themes are a <name>.kvconfig and a <name>.svg whose elements
 * are 9-patch pieces named "<element>-<state>[-top|-topleft|...]".
 */
gboolean lxp_kvantum_generate(const LxpSchemeColors *colors, int radius, const char *name,
                              const char *dir, GError **error);
