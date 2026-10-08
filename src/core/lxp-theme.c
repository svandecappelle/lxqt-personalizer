/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-theme.h"

#include "lxp-error.h"
#include "lxp-fileutil.h"
#include "lxp-paths.h"

#include <string.h>

struct _LxpTheme {
    GKeyFile *kf;
    char *name;
    char *dir;
};

static LxpTheme *theme_alloc(void)
{
    LxpTheme *theme = g_new0(LxpTheme, 1);
    theme->kf = g_key_file_new();
    return theme;
}

LxpTheme *lxp_theme_new(const char *name)
{
    LxpTheme *theme = theme_alloc();

    theme->name = g_strdup(name);
    g_key_file_set_string(theme->kf, LXP_THEME_GROUP, "Name", name);
    g_key_file_set_integer(theme->kf, LXP_THEME_GROUP, "FormatVersion",
                           LXP_THEME_FORMAT_VERSION);
    return theme;
}

LxpTheme *lxp_theme_load(const char *dir, GError **error)
{
    g_autoptr(LxpTheme) theme = theme_alloc();
    g_autofree char *path = g_build_filename(dir, LXP_THEME_MANIFEST, NULL);
    GError *local = NULL;
    int version;

    if (!g_key_file_load_from_file(theme->kf, path,
                                   G_KEY_FILE_KEEP_COMMENTS, error))
        return NULL;

    theme->name = g_key_file_get_string(theme->kf, LXP_THEME_GROUP, "Name", NULL);
    if (!theme->name || !*theme->name) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_INVALID,
                    "%s: missing [%s] Name", path, LXP_THEME_GROUP);
        return NULL;
    }

    version = g_key_file_get_integer(theme->kf, LXP_THEME_GROUP,
                                     "FormatVersion", &local);
    if (local) {
        g_clear_error(&local);
        version = 1;
    }
    if (version > LXP_THEME_FORMAT_VERSION) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_UNSUPPORTED,
                    "%s: format version %d is newer than supported (%d)",
                    path, version, LXP_THEME_FORMAT_VERSION);
        return NULL;
    }

    theme->dir = g_strdup(dir);
    return g_steal_pointer(&theme);
}

gboolean lxp_theme_save(LxpTheme *theme, const char *dir, GError **error)
{
    g_autofree char *path = g_build_filename(dir, LXP_THEME_MANIFEST, NULL);
    g_autofree char *data = lxp_theme_to_data(theme);

    if (!lxp_write_atomic(path, data, -1, 0644, error))
        return FALSE;

    if (theme->dir != dir) {
        g_free(theme->dir);
        theme->dir = g_strdup(dir);
    }
    return TRUE;
}

void lxp_theme_free(LxpTheme *theme)
{
    if (!theme)
        return;
    g_key_file_free(theme->kf);
    g_free(theme->name);
    g_free(theme->dir);
    g_free(theme);
}

const char *lxp_theme_get_name(const LxpTheme *theme)
{
    return theme->name;
}

const char *lxp_theme_get_dir(const LxpTheme *theme)
{
    return theme->dir;
}

GKeyFile *lxp_theme_get_keyfile(const LxpTheme *theme)
{
    return theme->kf;
}

char *lxp_theme_get_string(const LxpTheme *theme, const char *group,
                           const char *key)
{
    return g_key_file_get_string(theme->kf, group, key, NULL);
}

void lxp_theme_set_string(LxpTheme *theme, const char *group,
                          const char *key, const char *value)
{
    if (value)
        g_key_file_set_string(theme->kf, group, key, value);
    else
        g_key_file_remove_key(theme->kf, group, key, NULL);

    /* keep lxp_theme_get_name() in sync with the manifest */
    if (value && *value && g_str_equal(group, LXP_THEME_GROUP) && g_str_equal(key, "Name")) {
        g_free(theme->name);
        theme->name = g_strdup(value);
    }
}

char **lxp_theme_get_string_list(const LxpTheme *theme, const char *group,
                                 const char *key)
{
    return g_key_file_get_string_list(theme->kf, group, key, NULL, NULL);
}

void lxp_theme_set_string_list(LxpTheme *theme, const char *group,
                               const char *key, const char *const *values)
{
    g_key_file_set_string_list(theme->kf, group, key, values,
                               g_strv_length((char **)values));
}

void lxp_theme_remove_group(LxpTheme *theme, const char *group)
{
    g_key_file_remove_group(theme->kf, group, NULL);
}

char *lxp_theme_to_data(const LxpTheme *theme)
{
    return g_key_file_to_data(theme->kf, NULL, NULL);
}

gboolean lxp_theme_bundle_wallpaper(LxpTheme *theme, const char *dir, GError **error)
{
    g_autofree char *file = lxp_theme_get_string(theme, "Wallpaper", "file");
    g_autofree char *canonical_dir = NULL;
    g_autofree char *base = NULL;
    g_autofree char *name = NULL;
    g_autofree char *dest = NULL;
    const char *dot;

    if (!file || !g_path_is_absolute(file))
        return TRUE;
    canonical_dir = g_canonicalize_filename(dir, NULL);
    if (g_str_has_prefix(file, canonical_dir) &&
        file[strlen(canonical_dir)] == G_DIR_SEPARATOR) {
        /* already bundled: store it relative again */
        lxp_theme_set_string(theme, "Wallpaper", "file", file + strlen(canonical_dir) + 1);
        return TRUE;
    }

    base = g_path_get_basename(file);
    dot = strrchr(base, '.');
    name = g_strconcat("wallpaper", dot ? dot : "", NULL);
    dest = g_build_filename(dir, name, NULL);
    if (!lxp_copy_atomic(file, dest, 0644, error))
        return FALSE;
    lxp_theme_set_string(theme, "Wallpaper", "file", name);
    return TRUE;
}

char *lxp_theme_slugify(const char *name)
{
    g_autofree char *ascii = g_str_to_ascii(name, NULL);
    GString *slug = g_string_new(NULL);
    gboolean dash = FALSE;

    for (const char *p = ascii; *p; p++) {
        if (g_ascii_isalnum(*p)) {
            if (dash && slug->len > 0)
                g_string_append_c(slug, '-');
            g_string_append_c(slug, g_ascii_tolower(*p));
            dash = FALSE;
        } else {
            dash = TRUE;
        }
    }
    if (slug->len == 0)
        g_string_append(slug, "theme");
    return g_string_free(slug, FALSE);
}

static gboolean is_theme_dir(const char *dir)
{
    g_autofree char *manifest = g_build_filename(dir, LXP_THEME_MANIFEST, NULL);
    return g_file_test(manifest, G_FILE_TEST_IS_REGULAR);
}

char *lxp_theme_resolve(const char *name_or_path, GError **error)
{
    if (is_theme_dir(name_or_path))
        return g_canonicalize_filename(name_or_path, NULL);

    if (!strchr(name_or_path, G_DIR_SEPARATOR)) {
        g_autofree char *themes = lxp_paths_themes_dir();
        g_autofree char *slug = lxp_theme_slugify(name_or_path);
        g_autofree char *dir = g_build_filename(themes, slug, NULL);

        if (is_theme_dir(dir))
            return g_steal_pointer(&dir);
    }

    g_set_error(error, LXP_ERROR, LXP_ERROR_NOT_FOUND,
                "Theme “%s” not found", name_or_path);
    return NULL;
}

GPtrArray *lxp_theme_list_installed(void)
{
    g_autofree char *themes = lxp_paths_themes_dir();
    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
    g_autoptr(GDir) dir = g_dir_open(themes, 0, NULL);
    const char *name;

    if (!dir)
        return names;
    while ((name = g_dir_read_name(dir))) {
        g_autofree char *path = g_build_filename(themes, name, NULL);
        if (is_theme_dir(path))
            g_ptr_array_add(names, g_strdup(name));
    }
    g_ptr_array_sort_values(names, (GCompareFunc)g_strcmp0);
    return names;
}
