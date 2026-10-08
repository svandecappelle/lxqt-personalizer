/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <glib.h>

/*
 * Replace every {{name}} in <text> with the value of <name> in <vars>
 * (string -> string). Unknown names are an error, so typos in templates
 * are caught by the tests.
 */
char *lxp_template_expand(const char *text, GHashTable *vars, GError **error);

/* Expand the template stored as resource /lxp/templates/<name>. */
char *lxp_template_expand_resource(const char *name, GHashTable *vars,
                                   GError **error);
