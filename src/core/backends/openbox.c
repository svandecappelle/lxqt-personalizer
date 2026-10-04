/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Openbox window decorations: the <theme> section of ~/.config/openbox/rc.xml.
 * Elements are matched by local name because rc.xml uses a default
 * namespace (http://openbox.org/3.4/rc).
 */
#include "backends.h"

#include "lxp-error.h"
#include "lxp-paths.h"

#include <libxml/parser.h>
#include <libxml/tree.h>

#define RC_XML "openbox/rc.xml"
#define GROUP  "Openbox"

static xmlNode *find_child(xmlNode *parent, const char *name)
{
    for (xmlNode *n = parent ? parent->children : NULL; n; n = n->next)
        if (n->type == XML_ELEMENT_NODE && xmlStrEqual(n->name, BAD_CAST name))
            return n;
    return NULL;
}

/* Trimmed text content of <parent>/<name>, or NULL. */
static char *child_text(xmlNode *parent, const char *name)
{
    xmlNode *node = find_child(parent, name);
    xmlChar *content;
    char *text;

    if (!node)
        return NULL;
    content = xmlNodeGetContent(node);
    text = g_strstrip(g_strdup((const char *)content));
    xmlFree(content);
    return text;
}

static void capture_text(LxpTheme *theme, xmlNode *section,
                         const char *element, const char *theme_key)
{
    g_autofree char *text = child_text(section, element);

    if (text)
        lxp_theme_set_string(theme, GROUP, theme_key, text);
}

static void capture_fonts(LxpTheme *theme, xmlNode *section)
{
    for (xmlNode *n = section->children; n; n = n->next) {
        g_autofree char *key = NULL;
        g_autofree char *name = NULL, *size = NULL, *weight = NULL, *slant = NULL;
        xmlChar *place;

        if (n->type != XML_ELEMENT_NODE || !xmlStrEqual(n->name, BAD_CAST "font"))
            continue;
        place = xmlGetProp(n, BAD_CAST "place");
        if (!place)
            continue;
        key = g_strdup_printf("font_%s", (const char *)place);
        xmlFree(place);

        name = child_text(n, "name");
        size = child_text(n, "size");
        weight = child_text(n, "weight");
        slant = child_text(n, "slant");
        {
            const char *values[] = {
                name ? name : "", size ? size : "",
                weight ? weight : "", slant ? slant : "", NULL,
            };
            lxp_theme_set_string_list(theme, GROUP, key, values);
        }
    }
}

static gboolean openbox_available(void)
{
    g_autofree char *bin = g_find_program_in_path("openbox");
    return bin != NULL;
}

static GPtrArray *openbox_managed_files(void)
{
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);

    g_ptr_array_add(files, lxp_paths_user_config(RC_XML));
    return files;
}

static gboolean openbox_capture(LxpTheme *theme, GError **error)
{
    g_autofree char *path = lxp_paths_find_config(RC_XML);
    xmlDoc *doc;
    xmlNode *section;

    if (!path) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_NOT_FOUND,
                    "No Openbox configuration (%s) found", RC_XML);
        return FALSE;
    }

    doc = xmlReadFile(path, NULL, XML_PARSE_NONET | XML_PARSE_NOWARNING |
                                  XML_PARSE_NOERROR);
    if (!doc) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_INVALID,
                    "Failed to parse %s", path);
        return FALSE;
    }

    lxp_theme_remove_group(theme, GROUP);
    section = find_child(xmlDocGetRootElement(doc), "theme");
    if (section) {
        capture_text(theme, section, "name", "theme");
        capture_text(theme, section, "titleLayout", "title_layout");
        capture_text(theme, section, "keepBorder", "keep_border");
        capture_text(theme, section, "animateIconify", "animate_iconify");
        capture_fonts(theme, section);
    }

    xmlFreeDoc(doc);
    return TRUE;
}

const LxpBackend lxp_backend_openbox = {
    .id = "openbox",
    .description = "Openbox window decorations and title fonts",
    .available = openbox_available,
    .managed_files = openbox_managed_files,
    .capture = openbox_capture,
};
