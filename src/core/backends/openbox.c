/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Openbox window decorations: the <theme> section of ~/.config/openbox/rc.xml.
 * Elements are matched by local name because rc.xml uses a default
 * namespace (http://openbox.org/3.4/rc).
 */
#include "backends.h"

#include "lxp-error.h"
#include "lxp-fileutil.h"
#include "lxp-paths.h"
#include "lxp-system.h"

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <string.h>

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

/* Find or append <parent>/<name>, in the parent's namespace. */
static xmlNode *ensure_child(xmlNode *parent, const char *name)
{
    xmlNode *node = find_child(parent, name);

    if (!node)
        node = xmlNewChild(parent, parent->ns, BAD_CAST name, NULL);
    return node;
}

static void set_child_text(xmlNode *parent, const char *name, const char *text)
{
    xmlNode *node;

    if (!text || !*text)
        return;
    node = ensure_child(parent, name);
    xmlNodeSetContent(node, NULL);
    xmlNodeAddContent(node, BAD_CAST text);
}

static xmlNode *find_font(xmlNode *section, const char *place)
{
    for (xmlNode *n = section->children; n; n = n->next) {
        xmlChar *value;
        gboolean match;

        if (n->type != XML_ELEMENT_NODE || !xmlStrEqual(n->name, BAD_CAST "font"))
            continue;
        value = xmlGetProp(n, BAD_CAST "place");
        match = value && xmlStrEqual(value, BAD_CAST place);
        xmlFree(value);
        if (match)
            return n;
    }
    return NULL;
}

static void apply_fonts(const LxpTheme *theme, xmlNode *section)
{
    g_auto(GStrv) keys = g_key_file_get_keys(lxp_theme_get_keyfile(theme),
                                             GROUP, NULL, NULL);

    for (char **k = keys; k && *k; k++) {
        const char *place;
        g_auto(GStrv) values = NULL;
        xmlNode *font;

        if (!g_str_has_prefix(*k, "font_"))
            continue;
        place = *k + strlen("font_");
        values = lxp_theme_get_string_list(theme, GROUP, *k);
        if (!values || g_strv_length(values) < 2)
            continue;

        font = find_font(section, place);
        if (!font) {
            font = xmlNewChild(section, section->ns, BAD_CAST "font", NULL);
            xmlNewProp(font, BAD_CAST "place", BAD_CAST place);
        }
        set_child_text(font, "name", values[0]);
        set_child_text(font, "size", values[1]);
        if (values[2]) {
            set_child_text(font, "weight", values[2]);
            if (values[3])
                set_child_text(font, "slant", values[3]);
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

static gboolean openbox_check(const LxpTheme *theme, GError **error)
{
    g_autofree char *name = lxp_theme_get_string(theme, GROUP, "theme");

    if (!name)
        return TRUE;
    return lxp_backend_require(lxp_find_installed("themes", name, "openbox-3/themerc"),
                               "Openbox theme", name, error);
}

static gboolean openbox_apply(const LxpTheme *theme, GError **error)
{
    g_autofree char *target = lxp_paths_user_config(RC_XML);
    g_autofree char *source = NULL;
    xmlDoc *doc;
    xmlNode *root, *section;
    xmlChar *data = NULL;
    int len = 0;
    gboolean ok;

    if (!g_key_file_has_group(lxp_theme_get_keyfile(theme), GROUP))
        return TRUE;

    /* Start from the system rc.xml when the user has none yet. */
    source = lxp_paths_find_config(RC_XML);
    if (!source) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_NOT_FOUND,
                    "No Openbox configuration (%s) found", RC_XML);
        return FALSE;
    }

    doc = xmlReadFile(source, NULL, XML_PARSE_NONET | XML_PARSE_NOWARNING |
                                    XML_PARSE_NOERROR);
    if (!doc || !(root = xmlDocGetRootElement(doc))) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_INVALID,
                    "Failed to parse %s", source);
        xmlFreeDoc(doc);
        return FALSE;
    }

    section = ensure_child(root, "theme");
    {
        g_autofree char *name = lxp_theme_get_string(theme, GROUP, "theme");
        g_autofree char *layout = lxp_theme_get_string(theme, GROUP, "title_layout");
        g_autofree char *border = lxp_theme_get_string(theme, GROUP, "keep_border");
        g_autofree char *iconify = lxp_theme_get_string(theme, GROUP, "animate_iconify");

        set_child_text(section, "name", name);
        set_child_text(section, "titleLayout", layout);
        set_child_text(section, "keepBorder", border);
        set_child_text(section, "animateIconify", iconify);
    }
    apply_fonts(theme, section);

    xmlDocDumpFormatMemoryEnc(doc, &data, &len, "UTF-8", 0);
    xmlFreeDoc(doc);
    if (!data) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_FAILED,
                    "Failed to serialize %s", target);
        return FALSE;
    }
    ok = lxp_write_atomic(target, (const char *)data, len, 0644, error);
    xmlFree(data);
    return ok;
}

static gboolean openbox_reload(GError **error)
{
    g_auto(GStrv) running = lxp_process_find("openbox");
    const char *argv[] = { "openbox", "--reconfigure", NULL };

    if (!running)
        return TRUE;
    return lxp_run(argv, error);
}

const LxpBackend lxp_backend_openbox = {
    .id = "openbox",
    .description = "Openbox window decorations and title fonts",
    .available = openbox_available,
    .managed_files = openbox_managed_files,
    .check = openbox_check,
    .capture = openbox_capture,
    .apply = openbox_apply,
    .reload = openbox_reload,
};
