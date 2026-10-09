/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-surprise.h"

#include "lxp-color.h"
#include "lxp-scheme.h"
#include "lxp-wallgen.h"

static const char *const first_words[] = {
    "Velvet", "Arctic", "Amber", "Quiet", "Electric", "Misty", "Copper", "Lunar",
    "Coral", "Midnight", "Paper", "Neon", "Forest", "Desert", "Ocean", "Plum",
};
static const char *const second_words[] = {
    "Dusk", "Bloom", "Glass", "Harbor", "Ember", "Meadow", "Drift", "Signal",
    "Garden", "Orbit", "Sand", "Frost", "Canyon", "Tide", "Haze", "Pulse",
};

static const char *pick(GRand *rand, const char *const *list, int n)
{
    return list[g_rand_int_range(rand, 0, n)];
}

static void set(LxpTheme *theme, const char *group, const char *key, const char *value)
{
    lxp_theme_set_string(theme, group, key, value);
}

static void set_color(LxpTheme *theme, const char *key, const LxpColor *c)
{
    g_autofree char *hex = lxp_color_to_hex(c);
    set(theme, LXP_SCHEME_GROUP, key, hex);
}

static void set_int(LxpTheme *theme, const char *group, const char *key, int value)
{
    g_autofree char *text = g_strdup_printf("%d", value);
    set(theme, group, key, text);
}

char *lxp_surprise(LxpTheme *theme, GRand *rand, gboolean kvantum)
{
    static const double harmonies[] = { 0, 30, -30, 150, 180, 210 };
    static const char *const buttons[] = { "classic", "minimal", "dots" };
    static const char *const aligns[] = { "left", "center", "center" };
    static const char *const panels[] = { "underline", "pills", "flat" };
    static const char *const animations[] = { "none", "subtle", "smooth", "dynamic" };
    gboolean dark = g_rand_double(rand) < 0.6;
    double hue = g_rand_double_range(rand, 0, 360);
    double tint = g_rand_double_range(rand, 0.05, 0.22);
    double accent_hue = hue + harmonies[g_rand_int_range(rand, 0, G_N_ELEMENTS(harmonies))];
    double accent_sat = g_rand_double_range(rand, 0.55, 0.85);
    LxpColor bg, surface, fg, accent;
    char *name;

    if (dark) {
        double l = g_rand_double_range(rand, 0.11, 0.17);
        bg = lxp_color_from_hsl(hue, tint, l, 1);
        surface = lxp_color_from_hsl(hue, tint, l + g_rand_double_range(rand, -0.04, 0.06), 1);
        fg = lxp_color_from_hsl(hue, 0.10, 0.92, 1);
        accent = lxp_color_from_hsl(accent_hue, accent_sat, g_rand_double_range(rand, 0.58, 0.68), 1);
    } else {
        bg = lxp_color_from_hsl(hue, tint, g_rand_double_range(rand, 0.93, 0.97), 1);
        surface = lxp_color_from_hsl(hue, tint * 0.5, 0.99, 1);
        fg = lxp_color_from_hsl(hue, 0.12, 0.14, 1);
        accent = lxp_color_from_hsl(accent_hue, accent_sat, g_rand_double_range(rand, 0.40, 0.48), 1);
    }

    lxp_theme_remove_group(theme, LXP_SCHEME_GROUP);
    set_color(theme, "background", &bg);
    set_color(theme, "surface", &surface);
    set_color(theme, "foreground", &fg);
    set_color(theme, "accent", &accent);
    if (g_rand_double(rand) < 0.3)
        set_color(theme, "title", &accent);

    set(theme, LXP_SCHEME_GROUP, "buttons", pick(rand, buttons, 3));
    set(theme, LXP_SCHEME_GROUP, "title_align", pick(rand, aligns, 3));
    set(theme, LXP_SCHEME_GROUP, "title_style", g_rand_double(rand) < 0.2 ? "gradient" : "flat");
    set_int(theme, LXP_SCHEME_GROUP, "title_padding", g_rand_int_range(rand, 4, 9));
    set_int(theme, LXP_SCHEME_GROUP, "border_width", g_rand_int_range(rand, 0, 2));
    set(theme, LXP_SCHEME_GROUP, "panel_style", pick(rand, panels, 3));
    set(theme, LXP_SCHEME_GROUP, "wallpaper",
        lxp_wallgen_styles[g_rand_int_range(rand, 0, 4)]);
    set_int(theme, LXP_SCHEME_GROUP, "wallpaper_seed", g_rand_int_range(rand, 1, 1000000));
    set(theme, LXP_SCHEME_GROUP, "style", kvantum && g_rand_double(rand) < 0.7 ? "kvantum" : "Fusion");
    set_int(theme, LXP_SCHEME_GROUP, "widget_radius", g_rand_int_range(rand, 2, 11));
    if (g_rand_double(rand) < 0.5)
        set(theme, LXP_SCHEME_GROUP, "folder_color", "true");

    set_int(theme, "Compositor", "corner_radius", g_rand_int_range(rand, 0, 15));
    set(theme, "Compositor", "shadow", g_rand_double(rand) < 0.8 ? "true" : "false");
    set(theme, "Compositor", "blur", g_rand_double(rand) < 0.5 ? "true" : "false");
    set(theme, "Compositor", "animations", pick(rand, animations, 4));
    set_int(theme, "Panel", "opacity", g_rand_double(rand) < 0.5 ? 100 : g_rand_int_range(rand, 70, 95));

    name = g_strdup_printf("%s %s", pick(rand, first_words, G_N_ELEMENTS(first_words)),
                           pick(rand, second_words, G_N_ELEMENTS(second_words)));
    set(theme, LXP_THEME_GROUP, "Name", name);
    return name;
}
