/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-catalog.h"

#include <string.h>

/* $XDG_DATA_HOME/<kind>, ~/.<kind> (themes and icons), $XDG_DATA_DIRS/<kind> */
static GPtrArray *search_dirs(const char *kind)
{
    GPtrArray *dirs = g_ptr_array_new_with_free_func(g_free);

    g_ptr_array_add(dirs, g_build_filename(g_get_user_data_dir(), kind, NULL));
    if (g_str_equal(kind, "themes") || g_str_equal(kind, "icons")) {
        g_autofree char *legacy = g_strconcat(".", kind, NULL);
        g_ptr_array_add(dirs, g_build_filename(g_get_home_dir(), legacy, NULL));
    }
    for (const char *const *d = g_get_system_data_dirs(); *d; d++)
        g_ptr_array_add(dirs, g_build_filename(*d, kind, NULL));
    return dirs;
}

static void add_unique(GPtrArray *names, GHashTable *seen, const char *name)
{
    char *copy;

    if (g_hash_table_contains(seen, name))
        return;
    copy = g_strdup(name);
    g_ptr_array_add(names, copy);
    g_hash_table_add(seen, copy);
}

/* Directories <dir>/<name> of <kind> that contain <probe> (NULL: any). */
static void scan(GPtrArray *names, GHashTable *seen, const char *kind, const char *probe)
{
    g_autoptr(GPtrArray) dirs = search_dirs(kind);

    for (guint i = 0; i < dirs->len; i++) {
        g_autoptr(GDir) dir = g_dir_open(g_ptr_array_index(dirs, i), 0, NULL);
        const char *name;

        while (dir && (name = g_dir_read_name(dir))) {
            g_autofree char *path = g_build_filename(g_ptr_array_index(dirs, i), name,
                                                     probe, NULL);
            g_autofree char *base = g_build_filename(g_ptr_array_index(dirs, i), name, NULL);

            if (name[0] == '.' || g_str_has_prefix(name, "lxp-") ||
                !g_file_test(base, G_FILE_TEST_IS_DIR) ||
                (probe && !g_file_test(path, G_FILE_TEST_EXISTS)))
                continue;
            add_unique(names, seen, name);
        }
    }
}

/* "libkvantum.so" -> "kvantum", "breeze6.so" -> "Breeze" */
static char *style_name(const char *file)
{
    g_autofree char *base = g_strndup(file, strlen(file) - strlen(".so"));
    const char *name = g_str_has_prefix(base, "lib") ? base + 3 : base;
    size_t len = strlen(name);

    if (len > 1 && name[len - 1] == '6')
        len--;
    if (g_str_equal(name, "kvantum"))
        return g_strdup("kvantum");
    {
        char *out = g_strndup(name, len);
        out[0] = g_ascii_toupper(out[0]);
        return out;
    }
}

static void scan_qt_styles(GPtrArray *names, GHashTable *seen)
{
    static const char *const roots[] = {
        "/usr/lib/qt6/plugins", "/usr/lib64/qt6/plugins",
        "/usr/lib/x86_64-linux-gnu/qt6/plugins", "/usr/lib/aarch64-linux-gnu/qt6/plugins",
        NULL,
    };
    g_autoptr(GPtrArray) dirs = g_ptr_array_new_with_free_func(g_free);
    const char *env = g_getenv("QT_PLUGIN_PATH");

    add_unique(names, seen, "Fusion");
    add_unique(names, seen, "Windows");
    if (env) {
        g_auto(GStrv) parts = g_strsplit(env, ":", -1);
        for (char **p = parts; *p; p++)
            if (**p)
                g_ptr_array_add(dirs, g_build_filename(*p, "styles", NULL));
    }
    for (const char *const *r = roots; *r; r++)
        g_ptr_array_add(dirs, g_build_filename(*r, "styles", NULL));

    for (guint i = 0; i < dirs->len; i++) {
        g_autoptr(GDir) dir = g_dir_open(g_ptr_array_index(dirs, i), 0, NULL);
        const char *file;

        while (dir && (file = g_dir_read_name(dir))) {
            g_autofree char *name = NULL;

            if (!g_str_has_suffix(file, ".so"))
                continue;
            name = style_name(file);
            add_unique(names, seen, name);
        }
    }
}

/* Where Kvantum looks for themes: its config dir, then the data dirs. */
static GPtrArray *kvantum_dirs(void)
{
    GPtrArray *dirs = g_ptr_array_new_with_free_func(g_free);

    g_ptr_array_add(dirs, g_build_filename(g_get_user_config_dir(), "Kvantum", NULL));
    g_ptr_array_add(dirs, g_build_filename(g_get_user_data_dir(), "Kvantum", NULL));
    for (const char *const *d = g_get_system_data_dirs(); *d; d++)
        g_ptr_array_add(dirs, g_build_filename(*d, "Kvantum", NULL));
    return dirs;
}

static gboolean is_kvantum_theme(const char *dir, const char *name)
{
    g_autofree char *kvconfig = g_strconcat(name, ".kvconfig", NULL);
    g_autofree char *svg = g_strconcat(name, ".svg", NULL);
    g_autofree char *a = g_build_filename(dir, name, kvconfig, NULL);
    g_autofree char *b = g_build_filename(dir, name, svg, NULL);

    return g_file_test(a, G_FILE_TEST_IS_REGULAR) || g_file_test(b, G_FILE_TEST_IS_REGULAR);
}

char *lxp_catalog_find_kvantum(const char *name)
{
    g_autoptr(GPtrArray) dirs = kvantum_dirs();

    if (!name || !*name || strchr(name, G_DIR_SEPARATOR))
        return NULL;
    for (guint i = 0; i < dirs->len; i++)
        if (is_kvantum_theme(g_ptr_array_index(dirs, i), name))
            return g_build_filename(g_ptr_array_index(dirs, i), name, NULL);
    return NULL;
}

static void scan_kvantum(GPtrArray *names, GHashTable *seen)
{
    g_autoptr(GPtrArray) dirs = kvantum_dirs();

    add_unique(names, seen, "Default");
    for (guint i = 0; i < dirs->len; i++) {
        g_autoptr(GDir) dir = g_dir_open(g_ptr_array_index(dirs, i), 0, NULL);
        const char *name;

        while (dir && (name = g_dir_read_name(dir)))
            if (name[0] != '.' && is_kvantum_theme(g_ptr_array_index(dirs, i), name))
                add_unique(names, seen, name);
    }
}

GPtrArray *lxp_catalog_list(LxpCatalogKind kind)
{
    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
    g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);

    switch (kind) {
    case LXP_CATALOG_OPENBOX:
        scan(names, seen, "themes", "openbox-3/themerc");
        break;
    case LXP_CATALOG_GTK:
        add_unique(names, seen, "Adwaita");
        add_unique(names, seen, "HighContrast");
        /* gtk-3.0 alone may be a key-binding theme (Emacs, Default) */
        scan(names, seen, "themes", "gtk-3.0/gtk.css");
        scan(names, seen, "themes", "gtk-3.0/gtk.gresource");
        break;
    case LXP_CATALOG_LXQT:
        /* other things get unpacked there; a theme styles the panel */
        scan(names, seen, "lxqt/themes", "lxqt-panel.qss");
        break;
    case LXP_CATALOG_CURSORS:
        scan(names, seen, "icons", "cursors");
        break;
    case LXP_CATALOG_QT_STYLES:
        scan_qt_styles(names, seen);
        break;
    case LXP_CATALOG_KVANTUM:
        scan_kvantum(names, seen);
        break;
    }
    g_ptr_array_sort_values(names, (GCompareFunc)g_ascii_strcasecmp);
    return names;
}
