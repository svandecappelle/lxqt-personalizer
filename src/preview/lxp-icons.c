/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-icons.h"

#include "lxp-system.h"

#include <stdlib.h>

typedef enum { DIR_FIXED, DIR_SCALABLE, DIR_THRESHOLD } DirType;

typedef struct {
    char   *subdir;
    int     size, min_size, max_size, threshold;
    DirType type;
} IconDir;

typedef struct {
    GPtrArray *bases;    /* every <search path>/<theme> directory */
    GArray    *dirs;     /* IconDir */
    GStrv      inherits;
} IconTheme;

static GHashTable *themes;   /* name -> IconTheme* (NULL if missing) */
static GPtrArray *extra_dirs;  /* searched first */
static GHashTable *results;  /* "theme\nsize\nnames" -> path ("" if none) */

static void icon_dir_clear(IconDir *d)
{
    g_free(d->subdir);
}

static void icon_theme_free(IconTheme *t)
{
    if (!t)
        return;
    g_ptr_array_unref(t->bases);
    g_array_unref(t->dirs);
    g_strfreev(t->inherits);
    g_free(t);
}

static GPtrArray *search_bases(const char *name)
{
    GPtrArray *bases = g_ptr_array_new_with_free_func(g_free);
    char *path;

    for (guint i = 0; extra_dirs && i < extra_dirs->len; i++)
        g_ptr_array_add(bases, g_build_filename(extra_dirs->pdata[i], name, NULL));
    path = g_build_filename(g_get_home_dir(), ".icons", name, NULL);
    g_ptr_array_add(bases, path);
    g_ptr_array_add(bases, g_build_filename(g_get_user_data_dir(), "icons", name, NULL));
    for (const char *const *d = g_get_system_data_dirs(); *d; d++)
        g_ptr_array_add(bases, g_build_filename(*d, "icons", name, NULL));

    for (guint i = bases->len; i > 0; i--)
        if (!g_file_test(g_ptr_array_index(bases, i - 1), G_FILE_TEST_IS_DIR))
            g_ptr_array_remove_index(bases, i - 1);
    return bases;
}

/* Icon theme lists are comma separated, not GKeyFile's ';'. */
static GStrv get_list(GKeyFile *kf, const char *key)
{
    g_autofree char *value = g_key_file_get_string(kf, "Icon Theme", key, NULL);
    g_autoptr(GStrvBuilder) builder = g_strv_builder_new();
    g_auto(GStrv) parts = NULL;

    if (!value)
        return NULL;
    parts = g_strsplit(value, ",", -1);
    for (char **p = parts; *p; p++) {
        g_strstrip(*p);
        if (**p)
            g_strv_builder_add(builder, *p);
    }
    return g_strv_builder_end(builder);
}

static IconTheme *load_theme(const char *name)
{
    g_autoptr(GPtrArray) bases = search_bases(name);
    g_autoptr(GKeyFile) kf = g_key_file_new();
    g_auto(GStrv) dirs = NULL;
    IconTheme *t;
    gboolean loaded = FALSE;

    for (guint i = 0; i < bases->len && !loaded; i++) {
        g_autofree char *index = g_build_filename(g_ptr_array_index(bases, i),
                                                  "index.theme", NULL);
        loaded = g_key_file_load_from_file(kf, index, G_KEY_FILE_NONE, NULL);
    }
    if (!loaded)
        return NULL;

    t = g_new0(IconTheme, 1);
    t->bases = g_steal_pointer(&bases);
    t->dirs = g_array_new(FALSE, TRUE, sizeof(IconDir));
    g_array_set_clear_func(t->dirs, (GDestroyNotify)icon_dir_clear);
    t->inherits = get_list(kf, "Inherits");

    dirs = get_list(kf, "Directories");
    for (char **d = dirs; d && *d; d++) {
        IconDir dir = { 0 };
        g_autofree char *type = g_key_file_get_string(kf, *d, "Type", NULL);
        int scale = g_key_file_get_integer(kf, *d, "Scale", NULL);

        if (scale > 1)   /* @2x directories: same pixels, skip */
            continue;
        dir.subdir = g_strdup(*d);
        dir.size = g_key_file_get_integer(kf, *d, "Size", NULL);
        dir.min_size = g_key_file_has_key(kf, *d, "MinSize", NULL)
            ? g_key_file_get_integer(kf, *d, "MinSize", NULL) : dir.size;
        dir.max_size = g_key_file_has_key(kf, *d, "MaxSize", NULL)
            ? g_key_file_get_integer(kf, *d, "MaxSize", NULL) : dir.size;
        dir.threshold = g_key_file_has_key(kf, *d, "Threshold", NULL)
            ? g_key_file_get_integer(kf, *d, "Threshold", NULL) : 2;
        dir.type = !type || g_str_equal(type, "Threshold") ? DIR_THRESHOLD
                 : g_str_equal(type, "Scalable") ? DIR_SCALABLE : DIR_FIXED;
        g_array_append_val(t->dirs, dir);
    }
    return t;
}

static IconTheme *get_theme(const char *name)
{
    IconTheme *t;

    if (!themes)
        themes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                       (GDestroyNotify)icon_theme_free);
    if (g_hash_table_lookup_extended(themes, name, NULL, (gpointer *)&t))
        return t;
    t = load_theme(name);
    g_hash_table_insert(themes, g_strdup(name), t);
    return t;
}

/* Icon theme spec DirectorySizeDistance(). */
static int distance(const IconDir *d, int size)
{
    switch (d->type) {
    case DIR_FIXED:
        return abs(d->size - size);
    case DIR_SCALABLE:
        if (size < d->min_size)
            return d->min_size - size;
        if (size > d->max_size)
            return size - d->max_size;
        return 0;
    case DIR_THRESHOLD:
    default:
        if (size < d->size - d->threshold)
            return d->min_size - size;
        if (size > d->size + d->threshold)
            return size - d->max_size;
        return 0;
    }
}

static char *find_in_theme(const IconTheme *t, const char *name, int size)
{
    static const char *const exts[] = { ".svg", ".png", NULL };
    char *best = NULL;
    int best_dist = G_MAXINT;

    for (guint i = 0; i < t->dirs->len; i++) {
        const IconDir *d = &g_array_index(t->dirs, IconDir, i);
        int dist = distance(d, size);

        if (dist >= best_dist)
            continue;
        for (guint b = 0; b < t->bases->len; b++) {
            for (const char *const *e = exts; *e; e++) {
                g_autofree char *file = g_strconcat(name, *e, NULL);
                g_autofree char *path = g_build_filename(g_ptr_array_index(t->bases, b),
                                                         d->subdir, file, NULL);
                if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
                    g_free(best);
                    best = g_steal_pointer(&path);
                    best_dist = dist;
                    goto next_dir;
                }
            }
        }
next_dir:
        if (best_dist == 0)
            break;
    }
    return best;
}

static char *lookup_chain(const char *theme, const char *name, int size,
                          GHashTable *visited)
{
    IconTheme *t;

    if (g_hash_table_contains(visited, theme))
        return NULL;
    g_hash_table_add(visited, (gpointer)theme);
    if (!(t = get_theme(theme)))
        return NULL;

    {
        char *path = find_in_theme(t, name, size);
        if (path)
            return path;
    }
    for (char **parent = t->inherits; parent && *parent; parent++) {
        char *path = lookup_chain(*parent, name, size, visited);
        if (path)
            return path;
    }
    return NULL;
}

/*
 * Like Qt (which LXQt uses): each name is searched through the whole
 * inheritance chain before falling back to the next name.
 */
static char *lookup_name(const char *theme, const char *name, int size)
{
    g_autoptr(GHashTable) visited = g_hash_table_new(g_str_hash, g_str_equal);
    char *path = theme ? lookup_chain(theme, name, size, visited) : NULL;
    g_autofree char *file = NULL;
    g_autofree char *pixmap = NULL;

    if (path || (path = lookup_chain("hicolor", name, size, visited)))
        return path;

    file = g_strconcat(name, ".png", NULL);
    pixmap = g_build_filename("/usr/share/pixmaps", file, NULL);
    return g_file_test(pixmap, G_FILE_TEST_IS_REGULAR) ? g_steal_pointer(&pixmap) : NULL;
}

const char *lxp_icon_lookup(const char *theme, const char *const *names, int size)
{
    g_autofree char *joined = g_strjoinv("\n", (char **)names);
    g_autofree char *key = g_strdup_printf("%s\n%d\n%s", theme ? theme : "", size, joined);
    const char *cached;
    char *path = NULL;

    if (!results)
        results = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    if ((cached = g_hash_table_lookup(results, key)))
        return *cached ? cached : NULL;

    for (const char *const *n = names; !path && *n; n++)
        path = lookup_name(theme, *n, size);

    g_hash_table_insert(results, g_steal_pointer(&key), path ? path : g_strdup(""));
    return path;
}

GPtrArray *lxp_icon_themes_list(void)
{
    g_autoptr(GPtrArray) bases = g_ptr_array_new_with_free_func(g_free);
    g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);
    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);

    g_ptr_array_add(bases, g_build_filename(g_get_home_dir(), ".icons", NULL));
    g_ptr_array_add(bases, g_build_filename(g_get_user_data_dir(), "icons", NULL));
    for (const char *const *d = g_get_system_data_dirs(); *d; d++)
        g_ptr_array_add(bases, g_build_filename(*d, "icons", NULL));

    for (guint i = 0; i < bases->len; i++) {
        g_autoptr(GDir) dir = g_dir_open(g_ptr_array_index(bases, i), 0, NULL);
        const char *name;

        while (dir && (name = g_dir_read_name(dir))) {
            g_autofree char *index = g_build_filename(g_ptr_array_index(bases, i), name,
                                                      "index.theme", NULL);
            g_autoptr(GKeyFile) kf = g_key_file_new();

            if (g_hash_table_contains(seen, name) || g_str_equal(name, "default") ||
                !g_key_file_load_from_file(kf, index, G_KEY_FILE_NONE, NULL) ||
                !g_key_file_has_key(kf, "Icon Theme", "Directories", NULL))
                continue;
            g_ptr_array_add(names, g_strdup(name));
            g_hash_table_add(seen, g_ptr_array_index(names, names->len - 1));
        }
    }
    g_ptr_array_sort_values(names, (GCompareFunc)g_ascii_strcasecmp);
    return names;
}

void lxp_icons_add_search_dir(const char *dir)
{
    if (!extra_dirs)
        extra_dirs = g_ptr_array_new_with_free_func(g_free);
    if (g_ptr_array_find_with_equal_func(extra_dirs, dir, g_str_equal, NULL))
        return;
    g_ptr_array_insert(extra_dirs, 0, g_strdup(dir));
    lxp_icons_reset();
}

void lxp_icons_reset(void)
{
    g_clear_pointer(&themes, g_hash_table_unref);
    g_clear_pointer(&results, g_hash_table_unref);
}
