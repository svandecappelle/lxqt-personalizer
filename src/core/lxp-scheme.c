/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-scheme.h"

#include "lxp-error.h"
#include "lxp-fileutil.h"
#include "lxp-recolor.h"
#include "lxp-template.h"

#include <math.h>
#include <string.h>

#define ASSET_MARKER ".lxp-asset"

/* QColor "Invalid": let the generated LXQt theme style the panel. */
#define QCOLOR_DEFAULT "@Variant(\\0\\0\\0\\x43\\0\\xff\\xff\\0\\0\\0\\0\\0\\0\\0\\0)"

/* --- scheme <-> theme.ini ------------------------------------------------ */

static gboolean get_color(const LxpTheme *theme, const char *key, LxpColor *c,
                          gboolean required, GError **error)
{
    g_autofree char *value = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, key);

    if (value && lxp_color_parse(value, c))
        return TRUE;
    if (required)
        g_set_error(error, LXP_ERROR, LXP_ERROR_INVALID,
                    value ? "Invalid [Scheme] %s color “%s”" : "Missing [Scheme] %s color%s",
                    key, value ? value : "");
    return FALSE;
}

gboolean lxp_scheme_load(const LxpTheme *theme, LxpScheme *s, GError **error)
{
    memset(s, 0, sizeof(*s));
    if (!get_color(theme, "background", &s->background, TRUE, error) ||
        !get_color(theme, "surface", &s->surface, TRUE, error) ||
        !get_color(theme, "foreground", &s->foreground, TRUE, error) ||
        !get_color(theme, "accent", &s->accent, TRUE, error))
        return FALSE;
    s->has_accent_text = get_color(theme, "accent_text", &s->accent_text, FALSE, NULL);
    s->has_title = get_color(theme, "title", &s->title, FALSE, NULL);
    s->has_panel = get_color(theme, "panel", &s->panel, FALSE, NULL);
    s->font = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "font");
    s->style = lxp_theme_get_string(theme, LXP_SCHEME_GROUP, "style");
    {
        g_auto(GStrv) parts = lxp_theme_get_string_list(theme, LXP_SCHEME_GROUP, "generate");
        gboolean all = parts == NULL;

        s->keep_openbox = !all && !g_strv_contains((const char *const *)parts, "openbox");
        s->keep_gtk = !all && !g_strv_contains((const char *const *)parts, "gtk");
        s->keep_lxqt = !all && !g_strv_contains((const char *const *)parts, "lxqt");
    }
    return TRUE;
}

static void set_color(LxpTheme *theme, const char *key, const LxpColor *c, gboolean set)
{
    g_autofree char *hex = set ? lxp_color_to_hex(c) : NULL;
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, key, hex);
}

void lxp_scheme_save(const LxpScheme *s, LxpTheme *theme)
{
    set_color(theme, "background", &s->background, TRUE);
    set_color(theme, "surface", &s->surface, TRUE);
    set_color(theme, "foreground", &s->foreground, TRUE);
    set_color(theme, "accent", &s->accent, TRUE);
    set_color(theme, "accent_text", &s->accent_text, s->has_accent_text);
    set_color(theme, "title", &s->title, s->has_title);
    set_color(theme, "panel", &s->panel, s->has_panel);
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "font", s->font && *s->font ? s->font : NULL);
    lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "style",
                         s->style && *s->style ? s->style : NULL);

    if (!s->keep_openbox && !s->keep_gtk && !s->keep_lxqt) {
        lxp_theme_set_string(theme, LXP_SCHEME_GROUP, "generate", NULL);
    } else {
        const char *parts[4];
        int n = 0;

        if (!s->keep_openbox)
            parts[n++] = "openbox";
        if (!s->keep_gtk)
            parts[n++] = "gtk";
        if (!s->keep_lxqt)
            parts[n++] = "lxqt";
        parts[n] = NULL;
        lxp_theme_set_string_list(theme, LXP_SCHEME_GROUP, "generate", parts);
    }
}

gboolean lxp_scheme_from_palette(const LxpTheme *theme, LxpScheme *s)
{
    static const char *const keys[] = {
        "window_color", "base_color", "text_color", "highlight_color",
    };
    LxpColor *targets[] = { &s->background, &s->surface, &s->foreground, &s->accent };

    memset(s, 0, sizeof(*s));
    for (gsize i = 0; i < G_N_ELEMENTS(keys); i++) {
        g_autofree char *v = lxp_theme_get_string(theme, "LXQt Palette", keys[i]);
        if (!v || !lxp_qt_color_parse(v, targets[i]))
            return FALSE;
    }
    {
        g_autofree char *v = lxp_theme_get_string(theme, "LXQt Palette",
                                                  "highlighted_text_color");
        s->has_accent_text = v && lxp_qt_color_parse(v, &s->accent_text);
    }
    return TRUE;
}

void lxp_scheme_clear(LxpScheme *s)
{
    g_clear_pointer(&s->font, g_free);
    g_clear_pointer(&s->style, g_free);
}

/* --- derived colors ----------------------------------------------------- */

static LxpColor hex(guint32 v)
{
    LxpColor c = { ((v >> 16) & 0xff) / 255.0, ((v >> 8) & 0xff) / 255.0,
                   (v & 0xff) / 255.0, 1 };
    return c;
}

/* Text color for <bg>: <preferred> if readable, else black or white. */
static LxpColor readable_on(const LxpColor *bg, const LxpColor *preferred)
{
    LxpColor white = hex(0xffffff), black = hex(0x1a1a1a);

    if (preferred && lxp_color_contrast(bg, preferred) >= 4.5)
        return *preferred;
    return lxp_color_contrast(bg, &white) >= lxp_color_contrast(bg, &black) ? white : black;
}

void lxp_scheme_derive(const LxpScheme *s, LxpSchemeColors *c)
{
    LxpColor black = hex(0x000000);
    double h, sat, l;

    c->dark = lxp_color_luminance(&s->background) < lxp_color_luminance(&s->foreground);
    c->bg = s->background;
    c->surface = s->surface;
    c->fg = s->foreground;
    c->bg_alt = lxp_color_mix(&c->bg, &c->fg, 0.07);
    c->border = lxp_color_mix(&c->bg, &c->fg, 0.20);
    c->fg_dim = lxp_color_mix(&c->fg, &c->bg, 0.45);

    c->accent = s->accent;
    c->accent_fg = s->has_accent_text ? s->accent_text : readable_on(&c->accent, NULL);

    lxp_color_to_hsl(&c->accent, &h, &sat, &l);
    c->link = lxp_color_from_hsl(h, sat, c->dark ? MAX(l, 0.68) : MIN(l, 0.40), 1);
    c->link_visited = lxp_color_from_hsl(h + 45, sat, c->dark ? MAX(l, 0.68) : MIN(l, 0.40), 1);

    if (s->has_title) {
        c->title_active = s->title;
        c->title_active_fg = readable_on(&c->title_active, &c->fg);
        c->title_active_hover = lxp_color_mix(&c->title_active_fg, &c->title_active, 0.3);
    } else {
        c->title_active = c->bg_alt;
        c->title_active_fg = c->fg;
        c->title_active_hover = c->accent;
    }
    c->title_inactive = c->bg;
    c->title_inactive_fg = c->fg_dim;

    if (s->has_panel)
        c->panel = s->panel;
    else
        c->panel = c->dark ? lxp_color_mix(&c->bg, &black, 0.30)
                           : lxp_color_mix(&c->bg, &c->fg, 0.10);
    c->panel_fg = readable_on(&c->panel, &c->fg);
    c->panel_hover = lxp_color_mix(&c->panel, &c->panel_fg, 0.14);
}

/* --- generation --------------------------------------------------------- */

static void add_color(GHashTable *vars, const char *name, const LxpColor *c)
{
    g_hash_table_insert(vars, g_strdup(name), lxp_color_to_hex(c));
    g_hash_table_insert(vars, g_strconcat(name, "_rgb", NULL), lxp_color_to_rgb_triplet(c));
}

static GHashTable *template_vars(const char *name, const LxpSchemeColors *c)
{
    GHashTable *vars = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    g_hash_table_insert(vars, g_strdup("name"), g_strdup(name));
    add_color(vars, "bg", &c->bg);
    add_color(vars, "bg_alt", &c->bg_alt);
    add_color(vars, "surface", &c->surface);
    add_color(vars, "fg", &c->fg);
    add_color(vars, "fg_dim", &c->fg_dim);
    add_color(vars, "border", &c->border);
    add_color(vars, "accent", &c->accent);
    add_color(vars, "accent_fg", &c->accent_fg);
    add_color(vars, "link", &c->link);
    add_color(vars, "title_active", &c->title_active);
    add_color(vars, "title_active_fg", &c->title_active_fg);
    add_color(vars, "title_active_hover", &c->title_active_hover);
    add_color(vars, "title_inactive", &c->title_inactive);
    add_color(vars, "title_inactive_fg", &c->title_inactive_fg);
    add_color(vars, "panel", &c->panel);
    add_color(vars, "panel_fg", &c->panel_fg);
    add_color(vars, "panel_hover", &c->panel_hover);
    return vars;
}

static gboolean write_template(const char *tmpl, GHashTable *vars, const char *dir,
                               const char *relpath, GError **error)
{
    g_autofree char *text = lxp_template_expand_resource(tmpl, vars, error);
    g_autofree char *path = g_build_filename(dir, relpath, NULL);

    return text && lxp_write_atomic(path, text, -1, 0644, error);
}

static char *find_gtk_library(int major)
{
    static const char *const dirs[] = {
        "/usr/lib", "/usr/lib64", "/usr/lib/x86_64-linux-gnu",
        "/usr/lib/aarch64-linux-gnu", "/usr/local/lib", "/lib/x86_64-linux-gnu", NULL,
    };
    const char *name = major == 3 ? "libgtk-3.so.0" : "libgtk-4.so.1";

    for (const char *const *d = dirs; *d; d++) {
        char *path = g_build_filename(*d, name, NULL);
        if (g_file_test(path, G_FILE_TEST_EXISTS))
            return path;
        g_free(path);
    }
    return NULL;
}

/*
 * The stylesheet GTK <major> uses by default, extracted from the
 * installed library with gresource(1). NULL when unavailable.
 */
static char *extract_gtk_base_css(int major, gboolean dark)
{
    const char *base = major == 3 ? "/org/gtk/libgtk/theme/Adwaita/"
                                  : "/org/gtk/libgtk/theme/Default/";
    const char *file = major == 3 ? (dark ? "gtk-contained-dark.css" : "gtk-contained.css")
                                  : (dark ? "Default-dark.css" : "Default-light.css");
    g_autofree char *lib = find_gtk_library(major);
    g_autofree char *resource = g_strconcat(base, file, NULL);
    g_autofree char *out = NULL;
    g_autofree char *assets = NULL;
    g_autofree char *absolute = NULL;
    const char *argv[] = { "gresource", "extract", lib, resource, NULL };
    int status;

    if (!lib ||
        !g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
                      NULL, NULL, &out, NULL, &status, NULL) ||
        !g_spawn_check_wait_status(status, NULL) || !out || !*out)
        return NULL;

    /* Relative asset URLs only resolve inside GTK's own resources. */
    assets = g_strconcat("url(\"resource://", base, "assets/", NULL);
    {
        g_auto(GStrv) parts = g_strsplit(out, "url(\"assets/", -1);
        absolute = g_strjoinv(assets, parts);
    }
    return g_steal_pointer(&absolute);
}

/* Extracting takes a process spawn; the editor regenerates on every change. */
static char *gtk_base_css(int major, gboolean dark)
{
    static GHashTable *cache;
    G_LOCK_DEFINE_STATIC(cache);
    g_autofree char *key = g_strdup_printf("%d-%d", major, dark);
    char *css;

    G_LOCK(cache);
    if (!cache)
        cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    if (!g_hash_table_lookup_extended(cache, key, NULL, (gpointer *)&css)) {
        css = extract_gtk_base_css(major, dark);
        g_hash_table_insert(cache, g_strdup(key), css);
    }
    css = g_strdup(css);
    G_UNLOCK(cache);
    return css;
}

static gboolean write_gtk_css(int major, const char *name, const LxpSchemeColors *c,
                              GHashTable *vars, const char *theme_dir, GError **error)
{
    g_autofree char *base = gtk_base_css(major, c->dark);
    g_autofree char *css = NULL;
    g_autofree char *rel = g_strdup_printf("gtk-%d.0/gtk.css", major);
    g_autofree char *rel_dark = g_strdup_printf("gtk-%d.0/gtk-dark.css", major);
    g_autofree char *path = g_build_filename(theme_dir, rel, NULL);
    g_autofree char *path_dark = g_build_filename(theme_dir, rel_dark, NULL);
    LxpRecolorPalette from, to = { c->bg, c->surface, c->fg, c->accent };

    if (base && lxp_recolor_palette_from_gtk_css(base, &from)) {
        g_autofree char *recolored = lxp_recolor_css(base, &from, &to);
        g_autofree char *accent_fg = lxp_color_to_hex(&c->accent_fg);

        css = g_strdup_printf("/* %s — generated by lxqt-personalizer from GTK %d's own "
                              "stylesheet, regenerate instead of editing. */\n\n%s\n"
                              "@define-color theme_selected_fg_color %s;\n",
                              name, major, recolored, accent_fg);
    } else {
        css = lxp_template_expand_resource("gtk-fallback.css", vars, error);
        if (!css)
            return FALSE;
    }
    return lxp_write_atomic(path, css, -1, 0644, error) &&
           lxp_write_atomic(path_dark, css, -1, 0644, error);
}

/* Marks <dir> as installable and owned by lxqt-personalizer. */
static gboolean write_marker(const char *dir, GError **error)
{
    g_autofree char *path = g_build_filename(dir, ASSET_MARKER, NULL);
    return lxp_write_atomic(path, "", 0, 0644, error);
}

char *lxp_scheme_asset_name(const LxpTheme *theme)
{
    g_autofree char *slug = lxp_theme_slugify(lxp_theme_get_name(theme));
    return g_strconcat("lxp-", slug, NULL);
}

/* "Noto Sans Bold 10" -> family "Noto Sans Bold", size 10 */
static void split_font(const char *font, char **family, double *size)
{
    const char *space = font ? strrchr(font, ' ') : NULL;
    char *end;
    double v = space ? g_ascii_strtod(space + 1, &end) : 0;

    if (space && v > 0 && *end == '\0') {
        *family = g_strndup(font, space - font);
        *size = v;
    } else {
        *family = g_strdup(font && *font ? font : "Sans");
        *size = 10;
    }
}

static void set_theme_keys(LxpTheme *theme, const char *name, const LxpScheme *s,
                           const LxpSchemeColors *c)
{
    struct { const char *key; const LxpColor *color; } palette[] = {
        { "window_color", &c->bg },          { "window_text_color", &c->fg },
        { "base_color", &c->surface },       { "text_color", &c->fg },
        { "highlight_color", &c->accent },   { "highlighted_text_color", &c->accent_fg },
        { "link_color", &c->link },          { "link_visited_color", &c->link_visited },
    };

    if (!s->keep_lxqt)
        lxp_theme_set_string(theme, "LXQt", "theme", name);
    lxp_theme_set_string(theme, "LXQt", "style", s->style && *s->style ? s->style : "Fusion");
    lxp_theme_set_string(theme, "LXQt", "icon_follow_color_scheme", "true");
    lxp_theme_remove_group(theme, "LXQt Palette");
    for (gsize i = 0; i < G_N_ELEMENTS(palette); i++) {
        g_autofree char *v = lxp_color_to_hex(palette[i].color);
        lxp_theme_set_string(theme, "LXQt Palette", palette[i].key, v);
    }

    if (!s->keep_openbox)
        lxp_theme_set_string(theme, "Openbox", "theme", name);
    if (!s->keep_gtk)
        lxp_theme_set_string(theme, "GTK", "theme", name);
    lxp_theme_set_string(theme, "GTK", "prefer_dark", c->dark ? "true" : "false");

    /* the generated LXQt theme styles the panel */
    if (!s->keep_lxqt) {
        lxp_theme_set_string(theme, "Panel", "background-color", QCOLOR_DEFAULT);
        lxp_theme_set_string(theme, "Panel", "font-color", QCOLOR_DEFAULT);
    }

    if (s->font && *s->font) {
        static const char *const places[] = {
            "ActiveWindow", "InactiveWindow", "MenuHeader", "MenuItem",
            "ActiveOnScreenDisplay", "InactiveOnScreenDisplay",
        };
        g_autofree char *family = NULL;
        g_autofree char *qt = NULL;
        g_autofree char *size_text = NULL;
        double size;

        split_font(s->font, &family, &size);
        size_text = g_strdup_printf("%g", size);
        qt = g_strdup_printf("%s,%g,-1,5,50,0,0,0,0,0", family, size);
        lxp_theme_set_string(theme, "LXQt", "font", qt);
        lxp_theme_set_string(theme, "GTK", "font", s->font);
        for (gsize i = 0; i < G_N_ELEMENTS(places); i++) {
            g_autofree char *key = g_strconcat("font_", places[i], NULL);
            const char *weight = i < 3 ? "Bold" : "Normal";
            const char *values[] = { family, size_text, weight, "Normal", NULL };
            lxp_theme_set_string_list(theme, "Openbox", key, values);
        }
    }
}

/* Remove generated units of <kind> other than <keep> (e.g. after a rename). */
static gboolean remove_stale_units(const char *dir, const char *kind, const char *keep,
                                   GError **error)
{
    g_autofree char *parent = g_build_filename(dir, "data", kind, NULL);
    g_autoptr(GDir) d = g_dir_open(parent, 0, NULL);
    const char *name;

    while (d && (name = g_dir_read_name(d))) {
        g_autofree char *unit = g_build_filename(parent, name, NULL);
        g_autofree char *marker = g_build_filename(unit, ASSET_MARKER, NULL);

        if (g_str_has_prefix(name, "lxp-") && !g_str_equal(name, keep) &&
            g_file_test(marker, G_FILE_TEST_EXISTS) && !lxp_remove_tree(unit, error))
            return FALSE;
    }
    return TRUE;
}

gboolean lxp_scheme_generate(LxpTheme *theme, const char *dir, GError **error)
{
    LxpScheme s;
    LxpSchemeColors c;
    g_autofree char *name = NULL;
    g_autofree char *themes_dir = NULL;
    g_autofree char *lxqt_dir = NULL;
    g_autoptr(GHashTable) vars = NULL;
    gboolean ok;

    if (!lxp_scheme_load(theme, &s, error))
        return FALSE;
    lxp_scheme_derive(&s, &c);
    name = lxp_scheme_asset_name(theme);
    vars = template_vars(name, &c);

    themes_dir = g_build_filename(dir, "data", "themes", name, NULL);
    lxqt_dir = g_build_filename(dir, "data", "lxqt", "themes", name, NULL);

    ok = remove_stale_units(dir, "themes", name, error) &&
         remove_stale_units(dir, "lxqt/themes", name, error) &&
         lxp_remove_tree(themes_dir, error) && lxp_remove_tree(lxqt_dir, error);

    /* themes/<name> holds the Openbox and GTK parts */
    if (ok && (!s.keep_openbox || !s.keep_gtk))
        ok = write_marker(themes_dir, error) &&
             write_template("index.theme", vars, themes_dir, "index.theme", error);
    if (ok && !s.keep_openbox)
        ok = write_template("themerc", vars, themes_dir, "openbox-3/themerc", error);
    if (ok && !s.keep_gtk)
        ok = write_gtk_css(3, name, &c, vars, themes_dir, error) &&
             write_gtk_css(4, name, &c, vars, themes_dir, error);
    if (ok && !s.keep_lxqt)
        ok = write_marker(lxqt_dir, error) &&
             write_template("lxqt-panel.qss", vars, lxqt_dir, "lxqt-panel.qss", error) &&
             write_template("lxqt-runner.qss", vars, lxqt_dir, "lxqt-runner.qss", error) &&
             write_template("lxqt-notificationd.qss", vars, lxqt_dir,
                            "lxqt-notificationd.qss", error);

    if (ok)
        set_theme_keys(theme, name, &s, &c);
    lxp_scheme_clear(&s);
    return ok;
}
