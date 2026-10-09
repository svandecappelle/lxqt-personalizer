/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-themegen.h"

#include "lxp-error.h"
#include "lxp-icons.h"
#include "lxp-image.h"
#include "lxp-palette.h"
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

static gboolean generate_wallpaper(LxpTheme *theme, const char *dir, int width, int height,
                                   GError **error);

char *lxp_theme_variant_dir(const char *dir)
{
    g_autofree char *variant = g_build_filename(dir, LXP_VARIANT_DIR, NULL);
    g_autofree char *manifest = g_build_filename(variant, LXP_THEME_MANIFEST, NULL);

    return g_file_test(manifest, G_FILE_TEST_IS_REGULAR) ? g_steal_pointer(&variant) : NULL;
}

/*
 * The opposite light/dark version of <theme>: same settings, colors
 * flipped, named "<Name> Dark" or "<Name> Light", generated in <dir>.
 */
static gboolean generate_variant(LxpTheme *theme, const char *dir, int width, int height,
                                 GError **error)
{
    g_autofree char *data = lxp_theme_to_data(theme);
    g_autoptr(GKeyFile) kf = g_key_file_new();
    g_autoptr(LxpTheme) variant = NULL;
    g_autofree char *name = NULL;
    g_autofree char *file = NULL;
    LxpScheme s, flipped;
    LxpSchemeColors c;
    gboolean ok;

    if (!lxp_scheme_load(theme, &s, error))
        return FALSE;
    lxp_scheme_derive(&s, &c);
    name = g_strdup_printf("%s %s", lxp_theme_get_name(theme), c.dark ? "Light" : "Dark");

    /* a copy of the theme, then flipped colors */
    variant = lxp_theme_new(name);
    if (!g_key_file_load_from_data(lxp_theme_get_keyfile(variant), data, -1,
                                   G_KEY_FILE_KEEP_COMMENTS, error)) {
        lxp_scheme_clear(&s);
        return FALSE;
    }
    lxp_theme_set_string(variant, LXP_THEME_GROUP, "Name", name);
    lxp_theme_set_string(variant, LXP_SCHEME_GROUP, "variant", NULL);
    /* the GTK dark preference follows the flipped colors */
    lxp_theme_set_string(variant, LXP_SCHEME_GROUP, "prefer_dark", NULL);

    flipped = s;
    flipped.font = flipped.title_font = flipped.gtk_font = flipped.prefer_dark = NULL;
    flipped.style = flipped.wallpaper = flipped.title_style = flipped.title_align = NULL;
    flipped.buttons = flipped.panel_style = NULL;
    lxp_palette_flip(&flipped);
    {
        g_autofree char *bg = lxp_color_to_hex(&flipped.background);
        g_autofree char *surface = lxp_color_to_hex(&flipped.surface);
        g_autofree char *fg = lxp_color_to_hex(&flipped.foreground);
        g_autofree char *accent = lxp_color_to_hex(&flipped.accent);
        static const char *const optional[] = { "accent_text", "title", "panel", "link" };

        lxp_theme_set_string(variant, LXP_SCHEME_GROUP, "background", bg);
        lxp_theme_set_string(variant, LXP_SCHEME_GROUP, "surface", surface);
        lxp_theme_set_string(variant, LXP_SCHEME_GROUP, "foreground", fg);
        lxp_theme_set_string(variant, LXP_SCHEME_GROUP, "accent", accent);
        for (gsize i = 0; i < G_N_ELEMENTS(optional); i++)
            lxp_theme_set_string(variant, LXP_SCHEME_GROUP, optional[i], NULL);
    }
    lxp_scheme_clear(&s);

    /* an image wallpaper is shared with the main theme */
    file = lxp_theme_get_string(variant, "Wallpaper", "file");
    if (file && !g_path_is_absolute(file) && g_strcmp0(file, LXP_GENERATED_WALLPAPER) != 0) {
        g_autofree char *up = g_build_filename("..", file, NULL);
        lxp_theme_set_string(variant, "Wallpaper", "file", up);
    }

    ok = lxp_scheme_generate(variant, dir, error) &&
         generate_wallpaper(variant, dir, width, height, error) &&
         lxp_theme_save(variant, dir, error);
    return ok;
}

gboolean lxp_theme_generate(LxpTheme *theme, const char *dir, int width, int height,
                            GError **error)
{
    g_autofree char *variant_dir = g_build_filename(dir, LXP_VARIANT_DIR, NULL);
    g_autofree char *want = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "variant");

    if (!lxp_scheme_generate(theme, dir, error) ||
        !generate_wallpaper(theme, dir, width, height, error))
        return FALSE;
    /* recolored icons may sit at the same paths as before */
    lxp_icons_reset();
    lxp_image_cache_clear();

    if (!lxp_remove_tree(variant_dir, error))
        return FALSE;
    if (want && g_ascii_strcasecmp(want, "true") == 0)
        return generate_variant(theme, variant_dir, width, height, error);
    return TRUE;
}

static gboolean generate_wallpaper(LxpTheme *theme, const char *dir, int width, int height,
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
