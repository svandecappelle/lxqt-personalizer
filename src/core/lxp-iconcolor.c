/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-iconcolor.h"

#include "lxp-error.h"
#include "lxp-fileutil.h"
#include "lxp-recolor.h"
#include "lxp-system.h"

#include <string.h>

#define HUE_RANGE 35.0

static gboolean is_folder_icon(const char *file)
{
    return g_str_has_suffix(file, ".svg") &&
           (g_str_has_prefix(file, "folder") || g_str_has_prefix(file, "user-home") ||
            g_str_has_prefix(file, "user-desktop") || g_str_has_prefix(file, "inode-directory"));
}

/* Icon theme lists are comma separated. */
static GStrv get_list(GKeyFile *kf, const char *key)
{
    g_autofree char *value = g_key_file_get_string(kf, "Icon Theme", key, NULL);
    g_auto(GStrv) parts = value ? g_strsplit(value, ",", -1) : NULL;
    g_autoptr(GStrvBuilder) out = g_strv_builder_new();

    for (char **p = parts; p && *p; p++) {
        g_strstrip(*p);
        if (**p)
            g_strv_builder_add(out, *p);
    }
    return g_strv_builder_end(out);
}

/* The most common clearly colored hex value in <svg>: the folder color. */
static gboolean folder_color(const char *svg, LxpColor *out)
{
    g_autoptr(GHashTable) counts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GHashTableIter iter;
    gpointer key, value;
    int best = 0;

    for (const char *p = strchr(svg, '#'); p; p = strchr(p + 1, '#')) {
        g_autofree char *hex = g_strndup(p, 7);
        LxpColor c;
        double h, s, l;

        if (strlen(hex) != 7 || !lxp_color_parse(hex, &c))
            continue;
        lxp_color_to_hsl(&c, &h, &s, &l);
        if (s < 0.3 || l < 0.15 || l > 0.9)
            continue;
        {
            char *lower = g_ascii_strdown(hex, -1);
            int n = GPOINTER_TO_INT(g_hash_table_lookup(counts, lower)) + 1;
            g_hash_table_insert(counts, lower, GINT_TO_POINTER(n));
        }
    }
    g_hash_table_iter_init(&iter, counts);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        if (GPOINTER_TO_INT(value) > best) {
            best = GPOINTER_TO_INT(value);
            lxp_color_parse(key, out);
        }
    }
    return best > 0;
}

gboolean lxp_icons_folder_overlay(const char *base, const LxpColor *accent, const char *name,
                                  const char *dir, GError **error)
{
    g_autofree char *base_dir = lxp_find_installed("icons", base, "index.theme");
    g_autofree char *index_path = NULL;
    g_autoptr(GKeyFile) index = g_key_file_new();
    g_autoptr(GKeyFile) out = g_key_file_new();
    g_autoptr(GStrvBuilder) used = g_strv_builder_new();
    g_auto(GStrv) dirs = NULL;
    g_auto(GStrv) used_dirs = NULL;
    LxpColor source;
    gboolean have_source = FALSE;
    int copied = 0;

    if (!base_dir) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_NOT_FOUND, "Icon theme “%s” is not installed",
                    base);
        return FALSE;
    }
    /* [Icon Theme] first, then the directory sections */
    g_key_file_set_string(out, "Icon Theme", "Name", name);
    index_path = g_build_filename(base_dir, "index.theme", NULL);
    if (!g_key_file_load_from_file(index, index_path, G_KEY_FILE_NONE, error))
        return FALSE;
    dirs = get_list(index, "Directories");

    /* the reference color: the plain folder icon's */
    for (char **d = dirs; *d && !have_source; d++) {
        g_autofree char *folder = g_build_filename(base_dir, *d, "folder.svg", NULL);
        g_autofree char *svg = NULL;

        if (g_file_get_contents(folder, &svg, NULL, NULL))
            have_source = folder_color(svg, &source);
    }
    if (!have_source) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_NOT_FOUND,
                    "Icon theme “%s” has no SVG folder icon to take the color from", base);
        return FALSE;
    }

    for (char **d = dirs; *d; d++) {
        g_autofree char *context = g_key_file_get_string(index, *d, "Context", NULL);
        g_autofree char *src_dir = g_build_filename(base_dir, *d, NULL);
        g_autoptr(GDir) listing = NULL;
        const char *file;
        gboolean any = FALSE;

        if (g_strcmp0(context, "Places") != 0 || !(listing = g_dir_open(src_dir, 0, NULL)))
            continue;
        while ((file = g_dir_read_name(listing))) {
            g_autofree char *src = g_build_filename(src_dir, file, NULL);
            g_autofree char *dst = g_build_filename(dir, *d, file, NULL);
            g_autofree char *svg = NULL;
            g_autofree char *recolored = NULL;

            if (!is_folder_icon(file) || !g_file_get_contents(src, &svg, NULL, NULL))
                continue;
            recolored = lxp_recolor_hue(svg, &source, accent, HUE_RANGE);
            if (!lxp_write_atomic(dst, recolored, -1, 0644, error))
                return FALSE;
            any = TRUE;
            copied++;
        }
        if (any) {
            g_auto(GStrv) keys = g_key_file_get_keys(index, *d, NULL, NULL);

            g_strv_builder_add(used, *d);
            for (char **k = keys; k && *k; k++) {
                g_autofree char *v = g_key_file_get_value(index, *d, *k, NULL);
                g_key_file_set_value(out, *d, *k, v);
            }
        }
    }

    if (copied == 0) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_NOT_FOUND,
                    "Icon theme “%s” has no SVG folder icons to recolor", base);
        return FALSE;
    }

    used_dirs = g_strv_builder_end(used);
    {
        g_autofree char *list = g_strjoinv(",", used_dirs);
        g_autofree char *inherits = g_strdup_printf("%s,hicolor", base);
        g_autofree char *data = NULL;
        g_autofree char *path = g_build_filename(dir, "index.theme", NULL);

        g_key_file_set_string(out, "Icon Theme", "Comment",
                              "Folders in the theme's accent color, generated by lxqt-personalizer");
        g_key_file_set_string(out, "Icon Theme", "Inherits", inherits);
        g_key_file_set_string(out, "Icon Theme", "Directories", list);
        data = g_key_file_to_data(out, NULL, NULL);
        return lxp_write_atomic(path, data, -1, 0644, error);
    }
}
