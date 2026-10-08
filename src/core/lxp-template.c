/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-template.h"

#include "lxp-error.h"

#include <gio/gio.h>
#include <string.h>

char *lxp_template_expand(const char *text, GHashTable *vars, GError **error)
{
    g_autoptr(GString) out = g_string_sized_new(strlen(text) + 256);
    const char *p = text;

    for (;;) {
        const char *open = strstr(p, "{{");
        const char *close;
        g_autofree char *name = NULL;
        const char *value;

        if (!open) {
            g_string_append(out, p);
            break;
        }
        close = strstr(open + 2, "}}");
        if (!close) {
            g_set_error(error, LXP_ERROR, LXP_ERROR_INVALID, "Unterminated “{{” in template");
            return NULL;
        }
        g_string_append_len(out, p, open - p);
        name = g_strstrip(g_strndup(open + 2, close - open - 2));
        value = g_hash_table_lookup(vars, name);
        if (!value) {
            g_set_error(error, LXP_ERROR, LXP_ERROR_INVALID,
                        "Unknown template variable “%s”", name);
            return NULL;
        }
        g_string_append(out, value);
        p = close + 2;
    }
    return g_string_free(g_steal_pointer(&out), FALSE);
}

/* From the generated lxp-templates.c (manual registration: the file
 * would otherwise be dropped from the static library). */
void lxp_templates_register_resource(void);

char *lxp_template_expand_resource(const char *name, GHashTable *vars, GError **error)
{
    static gsize registered;
    g_autofree char *path = g_strconcat("/lxp/templates/", name, NULL);
    g_autoptr(GBytes) bytes = NULL;
    g_autofree char *text = NULL;

    if (g_once_init_enter(&registered)) {
        lxp_templates_register_resource();
        g_once_init_leave(&registered, 1);
    }
    bytes = g_resources_lookup_data(path, G_RESOURCE_LOOKUP_FLAGS_NONE, error);
    if (!bytes)
        return NULL;
    text = g_strndup(g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
    g_prefix_error(error, "%s: ", name);
    return lxp_template_expand(text, vars, error);
}
