/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-themegen.h"

#include "lxp-error.h"
#include "lxp-fileutil.h"
#include "lxp-paths.h"
#include "lxp-scheme.h"
#include "lxp-wallgen.h"

#include <glib/gstdio.h>

#define KEY_FILE ".wallpaper-key"

/* Everything the wallpaper depends on, to skip needless re-renders. */
static char *wallpaper_key(const LxpSchemeColors *c, const char *style, guint32 seed,
                           int width, int height)
{
    g_autofree char *bg = lxp_color_to_hex(&c->bg);
    g_autofree char *accent = lxp_color_to_hex(&c->accent);

    return g_strdup_printf("%s %u %dx%d %s %s %d", style, seed, width, height, bg, accent,
                           c->dark);
}

gboolean lxp_theme_generate(LxpTheme *theme, const char *dir, int width, int height,
                            GError **error)
{
    LxpScheme s;
    LxpSchemeColors c;
    g_autofree char *png = g_build_filename(dir, LXP_GENERATED_WALLPAPER, NULL);
    g_autofree char *key_path = g_build_filename(dir, KEY_FILE, NULL);
    g_autofree char *key = NULL;
    g_autofree char *old_key = NULL;
    g_autofree char *current = NULL;
    gboolean ok = TRUE;

    if (!lxp_scheme_generate(theme, dir, error))
        return FALSE;
    if (!lxp_scheme_load(theme, &s, error))
        return FALSE;

    if (!lxp_wallgen_is_style(s.wallpaper)) {
        /* a chosen image replaced the generated one: drop the old file */
        current = lxp_theme_get_string(theme, "Wallpaper", "file");
        if (g_strcmp0(current, LXP_GENERATED_WALLPAPER) != 0) {
            g_unlink(png);
            g_unlink(key_path);
        }
        lxp_scheme_clear(&s);
        return TRUE;
    }

    lxp_scheme_derive(&s, &c);
    key = wallpaper_key(&c, s.wallpaper, s.wallpaper_seed, width, height);
    g_file_get_contents(key_path, &old_key, NULL, NULL);
    if (g_strcmp0(key, old_key) != 0 || !g_file_test(png, G_FILE_TEST_IS_REGULAR))
        ok = g_mkdir_with_parents(dir, 0755) == 0 &&
             lxp_wallgen_render_png(&c, s.wallpaper, s.wallpaper_seed, width, height, png,
                                    error) &&
             g_file_set_contents(key_path, key, -1, error);
    if (ok) {
        lxp_theme_set_string(theme, "Wallpaper", "file", LXP_GENERATED_WALLPAPER);
        lxp_theme_set_string(theme, "Wallpaper", "mode", "zoom");
    }
    lxp_scheme_clear(&s);
    return ok;
}

char *lxp_theme_materialize(const char *dir, GError **error)
{
    g_autofree char *cache_root = NULL;
    g_autofree char *base = NULL;
    g_autofree char *cache = NULL;
    g_autofree char *source_ini = NULL;
    g_autofree char *stamp_path = NULL;
    g_autofree char *stamp = NULL;
    g_autoptr(LxpTheme) theme = NULL;

    if (!lxp_theme_is_builtin(dir))
        return g_strdup(dir);

    cache_root = lxp_paths_builtin_cache_dir();
    base = g_path_get_basename(dir);
    cache = g_build_filename(cache_root, base, NULL);
    stamp_path = g_build_filename(cache, ".source", NULL);
    {
        g_autofree char *manifest = g_build_filename(dir, LXP_THEME_MANIFEST, NULL);
        if (!g_file_get_contents(manifest, &source_ini, NULL, error))
            return NULL;
    }
    g_file_get_contents(stamp_path, &stamp, NULL, NULL);
    if (stamp && g_str_equal(stamp, source_ini))
        return g_steal_pointer(&cache);  /* generated from this very theme.ini */

    if (!lxp_remove_tree(cache, error) || !lxp_copy_tree(dir, cache, error) ||
        !(theme = lxp_theme_load(cache, error)) ||
        !lxp_theme_generate(theme, cache, LXP_WALLPAPER_WIDTH, LXP_WALLPAPER_HEIGHT, error) ||
        !lxp_theme_save(theme, cache, error) ||
        !g_file_set_contents(stamp_path, source_ini, -1, error))
        return NULL;
    return g_steal_pointer(&cache);
}
