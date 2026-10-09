/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-editor.h"

#include "lxp-apply.h"
#include "lxp-catalog.h"
#include "lxp-fileutil.h"
#include "lxp-icons.h"
#include "lxp-palette.h"
#include "lxp-paths.h"
#include "lxp-preview-area.h"
#include "lxp-scheme.h"
#include "lxp-surprise.h"
#include "lxp-themegen.h"
#include "lxp-wallgen.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PREVIEW_DELAY_MS 120
#define GENERATED_LABEL  "Generated from colors"

static const char *const wallpaper_modes[] = {
    "zoom", "fit", "stretch", "center", "tile", "color", NULL,
};
static const char *const panel_positions[] = { "Bottom", "Top", "Left", "Right", NULL };
static const char *const dark_modes[] = { "Automatic", "Light", "Dark", NULL };
static const char *const title_styles[] = { "Flat", "Gradient", NULL };
static const char *const title_aligns[] = { "Left", "Center", "Right", NULL };
static const char *const title_align_values[] = { "left", "center", "right", NULL };
static const char *const button_sets[] = { "Openbox classic", "Minimal", "Colored dots", NULL };
static const char *const button_set_values[] = { "classic", "minimal", "dots", NULL };
static const char *const panel_styles[] = { "Underline", "Pills", "Flat", NULL };
static const char *const panel_style_values[] = { "underline", "pills", "flat", NULL };
static const char *const wallpaper_sources[] = {
    "Image file", "Generated: gradient", "Generated: blobs", "Generated: waves",
    "Generated: geometric", NULL,
};
static const char *const animation_labels[] = {
    "Keep current", "None", "Subtle", "Smooth", "Dynamic", NULL,
};
static const char *const animation_values[] = {
    "custom", "none", "subtle", "smooth", "dynamic", NULL,
};
static const char *const tool_button_labels[] = {
    "Icons only", "Text only", "Text beside icons", "Text under icons",
    "Follow the widget style", NULL,
};
static const char *const tool_button_values[] = {
    "ToolButtonIconOnly", "ToolButtonTextOnly", "ToolButtonTextBesideIcon",
    "ToolButtonTextUnderIcon", "ToolButtonFollowStyle", NULL,
};

/* lxqt-panel "alignment": -1, 0, 1 */
static const char *const panel_alignments[] = { "Start", "Center", "End", NULL };

static const char *const ansi_names[] = {
    "Black", "Red", "Green", "Yellow", "Blue", "Magenta", "Cyan", "White",
};

static const struct {
    const char *name;
    const char *bg, *surface, *fg, *accent;
} presets[] = {
    { "Nord",            "#2e3440", "#3b4252", "#eceff4", "#88c0d0" },
    { "Dracula",         "#282a36", "#343746", "#f8f8f2", "#bd93f9" },
    { "Gruvbox",         "#282828", "#32302f", "#ebdbb2", "#d79921" },
    { "Solarized Light", "#fdf6e3", "#eee8d5", "#586e75", "#268bd2" },
    { "Catppuccin Latte","#eff1f5", "#e6e9ef", "#4c4f69", "#8839ef" },
    { "Adwaita",         "#f6f5f4", "#ffffff", "#2e3436", "#3584e4" },
};

/* The optional colors: a toggle for "automatic" and a color button. */
typedef struct {
    GtkWidget *automatic;
    GtkWidget *button;
} OptionalColor;

/* A font that can follow another one ("automatic") or be set. */
typedef struct {
    GtkWidget *automatic;
    GtkWidget *button;
} OptionalFont;

/*
 * A dropdown of installed names, optionally led by a special first row
 * ("Generated from colors", "Same as Qt applications") that reads back
 * as NULL.
 */
typedef struct {
    GtkWidget *dropdown;
    GtkStringList *model;
    GPtrArray *names;        /* names, in model order after the optional first row */
    gboolean has_generated;  /* has the special first row */
} Choice;

typedef struct {
    GtkWidget *window;
    char *dir;               /* theme being edited, NULL for a new one */
    char *work_dir;          /* temporary directory for the live preview */
    LxpTheme *theme;         /* working copy */
    LxpEditorSaved on_saved;
    gpointer user_data;
    guint preview_timer;
    gboolean loading;        /* suppress updates while filling the form */

    /* Colors */
    GtkWidget *name;
    GtkWidget *background, *surface, *foreground, *accent;
    GtkWidget *author, *comment;
    GtkWidget *variant, *variant_label;
    OptionalColor accent_text, title, panel_color, link;
    GtkWidget *warning;

    /* Terminal: 0-7 normal, 8-15 bright; derived until picked */
    GtkWidget *keep_terminal;
    GtkWidget *ansi[LXP_ANSI_COLORS];
    gboolean ansi_set[LXP_ANSI_COLORS];
    GtkWidget *ansi_reset[LXP_ANSI_COLORS / 2];

    /* Appearance */
    Choice openbox, gtk, lxqt, style, kvantum, icons, cursor, gtk_icons;
    GtkWidget *title_layout;
    OptionalFont title_font, gtk_font;
    GtkWidget *keep_border, *animate_iconify;
    GtkWidget *title_style, *title_padding, *border_width, *title_align, *buttons;
    OptionalColor menu_bg, menu_highlight;
    GtkWidget *dark_mode;
    GtkWidget *tool_buttons;
    GtkWidget *widget_radius;
    GtkWidget *icons_follow;
    GtkWidget *folder_color;
    GtkWidget *cursor_size;
    GtkWidget *font;

    /* Desktop */
    GtkWidget *wallpaper_label;
    GtkWidget *wallpaper_source;
    GtkWidget *wallpaper_choose;
    GtkWidget *wallpaper_shuffle;
    guint32 wallpaper_seed;
    char *wallpaper;
    GtkWidget *wallpaper_mode;
    GtkWidget *panel_position;
    GtkWidget *panel_size;
    GtkWidget *panel_opacity;
    GtkWidget *panel_icon_size;
    GtkWidget *panel_rows;
    GtkWidget *panel_width;
    GtkWidget *panel_width_percent;
    GtkWidget *panel_alignment;
    GtkWidget *panel_autohide;
    GtkWidget *panel_reserve;
    GtkWidget *panel_style;

    /* Effects */
    GtkWidget *corner_radius;
    GtkWidget *shadows;
    GtkWidget *shadow_strength;
    GtkWidget *shadow_radius;
    GtkWidget *shadow_offset_x;
    GtkWidget *shadow_offset_y;
    GtkWidget *inactive_opacity;
    GtkWidget *fading;
    GtkWidget *blur, *blur_strength;
    GtkWidget *animations;
    GtkWidget *from_wallpaper;

    GtkWidget *preview;
    GtkWidget *error;
} Editor;

static void choice_clear(Choice *c)
{
    g_clear_pointer(&c->names, g_ptr_array_unref);
    g_clear_object(&c->model);
}

static void editor_free(Editor *ed)
{
    Choice *choices[] = { &ed->openbox, &ed->gtk, &ed->lxqt, &ed->style,
                          &ed->kvantum, &ed->icons, &ed->cursor, &ed->gtk_icons };

    g_clear_handle_id(&ed->preview_timer, g_source_remove);
    if (ed->work_dir)
        lxp_remove_tree(ed->work_dir, NULL);
    g_free(ed->work_dir);
    g_free(ed->dir);
    g_free(ed->wallpaper);
    lxp_theme_free(ed->theme);
    for (gsize i = 0; i < G_N_ELEMENTS(choices); i++)
        choice_clear(choices[i]);
    g_free(ed);
}

/* --- choices ------------------------------------------------------------ */

static void on_changed(GObject *object, GParamSpec *pspec, gpointer data);

static void choice_init_full(Editor *ed, Choice *c, GPtrArray *names, const char *first)
{
    g_autoptr(GtkExpression) expression =
        gtk_property_expression_new(GTK_TYPE_STRING_OBJECT, NULL, "string");

    c->names = names;
    c->has_generated = first != NULL;
    c->model = gtk_string_list_new(NULL);
    if (first)
        gtk_string_list_append(c->model, first);
    for (guint i = 0; i < names->len; i++)
        gtk_string_list_append(c->model, g_ptr_array_index(names, i));

    c->dropdown = gtk_drop_down_new(G_LIST_MODEL(g_object_ref(c->model)), NULL);
    gtk_drop_down_set_expression(GTK_DROP_DOWN(c->dropdown), expression);
    gtk_drop_down_set_enable_search(GTK_DROP_DOWN(c->dropdown), names->len > 8);
    g_signal_connect(c->dropdown, "notify::selected", G_CALLBACK(on_changed), ed);
}

static void choice_init(Editor *ed, Choice *c, GPtrArray *names, gboolean has_generated)
{
    choice_init_full(ed, c, names, has_generated ? GENERATED_LABEL : NULL);
}

/* Selected name, or NULL for the special first row / nothing. */
static const char *choice_get(const Choice *c)
{
    guint i = gtk_drop_down_get_selected(GTK_DROP_DOWN(c->dropdown));

    if (i == GTK_INVALID_LIST_POSITION || (c->has_generated && i == 0))
        return NULL;
    i -= c->has_generated ? 1 : 0;
    return i < c->names->len ? g_ptr_array_index(c->names, i) : NULL;
}

/* Select <name>; unknown names (e.g. not installed here) are added. */
static void choice_set(Choice *c, const char *name)
{
    guint index;
    guint offset = c->has_generated ? 1 : 0;

    if (!name) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(c->dropdown), 0);
        return;
    }
    if (!g_ptr_array_find_with_equal_func(c->names, name, g_str_equal, &index)) {
        g_ptr_array_add(c->names, g_strdup(name));
        gtk_string_list_append(c->model, name);
        index = c->names->len - 1;
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(c->dropdown), index + offset);
}

/* --- form <-> values ---------------------------------------------------- */

static void to_rgba(const LxpColor *c, GdkRGBA *rgba)
{
    rgba->red = c->r;
    rgba->green = c->g;
    rgba->blue = c->b;
    rgba->alpha = 1;
}

static LxpColor from_button(GtkWidget *button)
{
    const GdkRGBA *rgba = gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(button));
    LxpColor c = { rgba->red, rgba->green, rgba->blue, 1 };
    return c;
}

static void set_button(GtkWidget *button, const LxpColor *c)
{
    GdkRGBA rgba;
    to_rgba(c, &rgba);
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(button), &rgba);
}

static void set_optional(OptionalColor *o, gboolean has, const LxpColor *c, const LxpColor *fallback)
{
    gtk_check_button_set_active(GTK_CHECK_BUTTON(o->automatic), !has);
    set_button(o->button, has ? c : fallback);
}

static gboolean get_optional(OptionalColor *o, LxpColor *c)
{
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(o->automatic)))
        return FALSE;
    *c = from_button(o->button);
    return TRUE;
}

static char *get_font(GtkWidget *button)
{
    PangoFontDescription *font =
        gtk_font_dialog_button_get_font_desc(GTK_FONT_DIALOG_BUTTON(button));
    return font ? pango_font_description_to_string(font) : NULL;
}

static void set_font(GtkWidget *button, const char *desc)
{
    PangoFontDescription *font = pango_font_description_from_string(desc);
    gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(button), font);
    pango_font_description_free(font);
}

/* NULL when automatic. */
static char *get_optional_font(OptionalFont *o)
{
    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(o->automatic)))
        return NULL;
    return get_font(o->button);
}

static void set_optional_font(OptionalFont *o, const char *value, const char *fallback)
{
    gtk_check_button_set_active(GTK_CHECK_BUTTON(o->automatic), value == NULL);
    set_font(o->button, value ? value : fallback);
}

static int string_index(const char *const *list, const char *value)
{
    for (int i = 0; list[i]; i++)
        if (g_strcmp0(list[i], value) == 0)
            return i;
    return 0;
}

static int spin_value(GtkWidget *spin)
{
    return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
}

/* Openbox title layout: letters from NDSLIMC, each at most once. */
static gboolean valid_title_layout(const char *layout)
{
    for (const char *p = layout; *p; p++)
        if (!strchr("NDSLIMC", *p) || strchr(p + 1, *p))
            return FALSE;
    return TRUE;
}

static void read_form(Editor *ed, LxpScheme *s)
{
    PangoFontDescription *font =
        gtk_font_dialog_button_get_font_desc(GTK_FONT_DIALOG_BUTTON(ed->font));

    memset(s, 0, sizeof(*s));
    s->background = from_button(ed->background);
    s->surface = from_button(ed->surface);
    s->foreground = from_button(ed->foreground);
    s->accent = from_button(ed->accent);
    s->has_accent_text = get_optional(&ed->accent_text, &s->accent_text);
    s->has_title = get_optional(&ed->title, &s->title);
    s->has_panel = get_optional(&ed->panel_color, &s->panel);
    s->has_link = get_optional(&ed->link, &s->link);
    s->has_menu = get_optional(&ed->menu_bg, &s->menu);
    s->has_menu_highlight = get_optional(&ed->menu_highlight, &s->menu_highlight);
    for (int i = 0; i < LXP_ANSI_COLORS; i++)
        if ((s->has_ansi[i] = ed->ansi_set[i]))
            s->ansi[i] = from_button(ed->ansi[i]);
    s->font = font ? pango_font_description_to_string(font) : NULL;
    s->title_font = get_optional_font(&ed->title_font);
    s->gtk_font = get_optional_font(&ed->gtk_font);
    switch (gtk_drop_down_get_selected(GTK_DROP_DOWN(ed->dark_mode))) {
    case 1: s->prefer_dark = g_strdup("false"); break;
    case 2: s->prefer_dark = g_strdup("true"); break;
    default: break;
    }
    s->style = g_strdup(choice_get(&ed->style));
    if (gtk_drop_down_get_selected(GTK_DROP_DOWN(ed->title_style)) == 1)
        s->title_style = g_strdup("gradient");
    s->title_padding = spin_value(ed->title_padding);
    s->border_width = spin_value(ed->border_width);
    s->has_border_width = TRUE;
    s->title_align = g_strdup(title_align_values[
        MIN(gtk_drop_down_get_selected(GTK_DROP_DOWN(ed->title_align)), 2u)]);
    s->buttons = g_strdup(button_set_values[
        MIN(gtk_drop_down_get_selected(GTK_DROP_DOWN(ed->buttons)), 2u)]);
    {
        guint source = gtk_drop_down_get_selected(GTK_DROP_DOWN(ed->wallpaper_source));
        if (source >= 1 && source <= 4) {
            s->wallpaper = g_strdup(lxp_wallgen_styles[source - 1]);
            s->wallpaper_seed = ed->wallpaper_seed;
        }
    }
    s->keep_openbox = choice_get(&ed->openbox) != NULL;
    s->keep_gtk = choice_get(&ed->gtk) != NULL;
    s->keep_lxqt = choice_get(&ed->lxqt) != NULL;
    s->keep_kvantum = choice_get(&ed->kvantum) != NULL;
    s->keep_terminal = gtk_switch_get_active(GTK_SWITCH(ed->keep_terminal));
    s->folder_color = gtk_switch_get_active(GTK_SWITCH(ed->folder_color));
    s->icons_base = g_strdup(choice_get(&ed->icons));
    s->widget_radius = spin_value(ed->widget_radius);
    s->panel_style = g_strdup(panel_style_values[
        MIN(gtk_drop_down_get_selected(GTK_DROP_DOWN(ed->panel_style)), 2u)]);
    s->has_widget_radius = TRUE;
}

static void set_int(LxpTheme *theme, const char *group, const char *key, int value)
{
    g_autofree char *text = g_strdup_printf("%d", value);
    lxp_theme_set_string(theme, group, key, text);
}

/* Copy the form into the working theme (everything except generation). */
static void update_theme(Editor *ed)
{
    LxpScheme s;
    LxpTheme *t = ed->theme;
    const char *name = gtk_editable_get_text(GTK_EDITABLE(ed->name));
    const char *layout = gtk_editable_get_text(GTK_EDITABLE(ed->title_layout));
    guint mode = gtk_drop_down_get_selected(GTK_DROP_DOWN(ed->wallpaper_mode));
    guint position = gtk_drop_down_get_selected(GTK_DROP_DOWN(ed->panel_position));
    const char *value;
    char opacity[G_ASCII_DTOSTR_BUF_SIZE];

    read_form(ed, &s);
    lxp_scheme_save(&s, t);
    lxp_scheme_clear(&s);

    lxp_theme_set_string(t, LXP_THEME_GROUP, "Name", *name ? name : "Untitled");
    lxp_theme_set_string(t, LXP_SCHEME_GROUP, "variant",
                         gtk_switch_get_active(GTK_SWITCH(ed->variant)) ? "true" : NULL);
    value = gtk_editable_get_text(GTK_EDITABLE(ed->author));
    lxp_theme_set_string(t, LXP_THEME_GROUP, "Author", *value ? value : NULL);
    value = gtk_editable_get_text(GTK_EDITABLE(ed->comment));
    lxp_theme_set_string(t, LXP_THEME_GROUP, "Comment", *value ? value : NULL);

    lxp_theme_set_string(t, "Openbox", "keep_border",
                         gtk_switch_get_active(GTK_SWITCH(ed->keep_border)) ? "yes" : "no");
    lxp_theme_set_string(t, "Openbox", "animate_iconify",
                         gtk_switch_get_active(GTK_SWITCH(ed->animate_iconify)) ? "yes" : "no");
    lxp_theme_set_string(t, "LXQt", "tool_button_style", tool_button_values[
        MIN(gtk_drop_down_get_selected(GTK_DROP_DOWN(ed->tool_buttons)), 4u)]);
    lxp_theme_set_string(t, "LXQt", "icon_follow_color_scheme",
                         gtk_switch_get_active(GTK_SWITCH(ed->icons_follow)) ? "true" : "false");

    /* installed themes chosen instead of generated ones */
    if ((value = choice_get(&ed->openbox)))
        lxp_theme_set_string(t, "Openbox", "theme", value);
    if ((value = choice_get(&ed->gtk)))
        lxp_theme_set_string(t, "GTK", "theme", value);
    if ((value = choice_get(&ed->lxqt)))
        lxp_theme_set_string(t, "LXQt", "theme", value);
    if (*layout && valid_title_layout(layout))
        lxp_theme_set_string(t, "Openbox", "title_layout", layout);

    /* the Kvantum theme only matters with the kvantum style */
    /* generation names the generated Kvantum theme; else the chosen one */
    value = choice_get(&ed->style);
    if (value && g_ascii_strcasecmp(value, "kvantum") == 0 && choice_get(&ed->kvantum))
        lxp_theme_set_string(t, "Kvantum", "theme", choice_get(&ed->kvantum));
    else if (!value || g_ascii_strcasecmp(value, "kvantum") != 0)
        lxp_theme_remove_group(t, "Kvantum");

    if ((value = choice_get(&ed->icons))) {
        lxp_theme_set_string(t, "LXQt", "icon_theme", value);
        lxp_theme_set_string(t, "GTK", "icon_theme", value);
    }
    if ((value = choice_get(&ed->gtk_icons)))
        lxp_theme_set_string(t, "GTK", "icon_theme", value);
    if ((value = choice_get(&ed->cursor)))
        lxp_theme_set_string(t, "Cursor", "theme", value);
    set_int(t, "Cursor", "size", spin_value(ed->cursor_size));

    if (gtk_drop_down_get_selected(GTK_DROP_DOWN(ed->wallpaper_source)) == 0)
        lxp_theme_set_string(t, "Wallpaper", "file", ed->wallpaper);
    lxp_theme_set_string(t, "Wallpaper", "mode", wallpaper_modes[mode]);
    lxp_theme_set_string(t, "Panel", "position", panel_positions[position]);
    set_int(t, "Panel", "panelSize", spin_value(ed->panel_size));
    set_int(t, "Panel", "opacity", spin_value(ed->panel_opacity));
    set_int(t, "Panel", "iconSize", spin_value(ed->panel_icon_size));
    set_int(t, "Panel", "lineCount", spin_value(ed->panel_rows));
    set_int(t, "Panel", "width", spin_value(ed->panel_width));
    lxp_theme_set_string(t, "Panel", "width-percent",
                         gtk_switch_get_active(GTK_SWITCH(ed->panel_width_percent)) ? "true" : "false");
    set_int(t, "Panel", "alignment",
            (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(ed->panel_alignment)) - 1);
    lxp_theme_set_string(t, "Panel", "hidable",
                         gtk_switch_get_active(GTK_SWITCH(ed->panel_autohide)) ? "true" : "false");
    lxp_theme_set_string(t, "Panel", "reserve-space",
                         gtk_switch_get_active(GTK_SWITCH(ed->panel_reserve)) ? "true" : "false");

    set_int(t, "Compositor", "corner_radius", spin_value(ed->corner_radius));
    lxp_theme_set_string(t, "Compositor", "shadow",
                         gtk_switch_get_active(GTK_SWITCH(ed->shadows)) ? "true" : "false");
    g_ascii_formatd(opacity, sizeof(opacity), "%.2f",
                    gtk_range_get_value(GTK_RANGE(ed->shadow_strength)) / 100.0);
    lxp_theme_set_string(t, "Compositor", "shadow_opacity", opacity);
    lxp_theme_set_string(t, "Compositor", "fading",
                         gtk_switch_get_active(GTK_SWITCH(ed->fading)) ? "true" : "false");
    set_int(t, "Compositor", "shadow_radius", spin_value(ed->shadow_radius));
    set_int(t, "Compositor", "shadow_offset_x", spin_value(ed->shadow_offset_x));
    set_int(t, "Compositor", "shadow_offset_y", spin_value(ed->shadow_offset_y));
    g_ascii_formatd(opacity, sizeof(opacity), "%.2f",
                    spin_value(ed->inactive_opacity) / 100.0);
    lxp_theme_set_string(t, "Compositor", "inactive_opacity", opacity);
    lxp_theme_set_string(t, "Compositor", "blur",
                         gtk_switch_get_active(GTK_SWITCH(ed->blur)) ? "true" : "false");
    set_int(t, "Compositor", "blur_strength", spin_value(ed->blur_strength));
    {
        guint anim = gtk_drop_down_get_selected(GTK_DROP_DOWN(ed->animations));
        lxp_theme_set_string(t, "Compositor", "animations",
                             anim == 0 ? NULL : animation_values[MIN(anim, 4u)]);
    }
}

/* Automatic colors show the value that will actually be used. */
static void show_automatic_colors(Editor *ed)
{
    LxpScheme s;
    LxpSchemeColors c;
    struct { OptionalColor *o; const LxpColor *value; } rows[] = {
        { &ed->accent_text, &c.accent_fg },
        { &ed->title, &c.title_active },
        { &ed->panel_color, &c.panel },
        { &ed->link, &c.link },
        { &ed->menu_bg, &c.menu_bg },
        { &ed->menu_highlight, &c.menu_active },
    };
    gboolean was_loading = ed->loading;

    read_form(ed, &s);
    lxp_scheme_derive(&s, &c);
    ed->loading = TRUE;
    for (gsize i = 0; i < G_N_ELEMENTS(rows); i++)
        if (gtk_check_button_get_active(GTK_CHECK_BUTTON(rows[i].o->automatic)))
            set_button(rows[i].o->button, rows[i].value);
    for (int i = 0; i < LXP_ANSI_COLORS; i++)
        if (!ed->ansi_set[i])
            set_button(ed->ansi[i], &c.ansi[i]);
    ed->loading = was_loading;
    lxp_scheme_clear(&s);
}

static void sync_sensitivity(Editor *ed)
{
    const char *style = choice_get(&ed->style);

    gtk_widget_set_sensitive(ed->kvantum.dropdown,
                             style && g_ascii_strcasecmp(style, "kvantum") == 0);
    gtk_widget_set_sensitive(ed->panel_style, choice_get(&ed->lxqt) == NULL);
    gtk_widget_set_sensitive(ed->widget_radius,
                             style && g_ascii_strcasecmp(style, "kvantum") == 0 &&
                             choice_get(&ed->kvantum) == NULL);
    {
        gboolean generated = choice_get(&ed->openbox) == NULL;
        gboolean image = gtk_drop_down_get_selected(GTK_DROP_DOWN(ed->wallpaper_source)) == 0;
        GtkWidget *window_style[] = { ed->title_style, ed->title_padding, ed->border_width,
                                      ed->title_align, ed->buttons };

        for (gsize i = 0; i < G_N_ELEMENTS(window_style); i++)
            gtk_widget_set_sensitive(window_style[i], generated);
        gtk_widget_set_sensitive(ed->menu_bg.automatic, generated);
        gtk_widget_set_sensitive(ed->menu_bg.button, generated);
        gtk_widget_set_sensitive(ed->menu_highlight.automatic, generated);
        gtk_widget_set_sensitive(ed->menu_highlight.button, generated);
        gtk_widget_set_sensitive(ed->wallpaper_choose, image);
        gtk_widget_set_sensitive(ed->wallpaper_mode, image);
        gtk_widget_set_visible(ed->wallpaper_shuffle, !image);
        gtk_widget_set_sensitive(ed->from_wallpaper, image && ed->wallpaper != NULL);
        gtk_widget_set_sensitive(ed->blur_strength, gtk_switch_get_active(GTK_SWITCH(ed->blur)));
    }
    gtk_widget_set_sensitive(ed->title_font.button,
        !gtk_check_button_get_active(GTK_CHECK_BUTTON(ed->title_font.automatic)));
    gtk_widget_set_sensitive(ed->gtk_font.button,
        !gtk_check_button_get_active(GTK_CHECK_BUTTON(ed->gtk_font.automatic)));
    {
        gboolean generated = !gtk_switch_get_active(GTK_SWITCH(ed->keep_terminal));

        for (int i = 0; i < LXP_ANSI_COLORS; i++)
            gtk_widget_set_sensitive(ed->ansi[i], generated);
        for (int i = 0; i < LXP_ANSI_COLORS / 2; i++)
            gtk_widget_set_sensitive(ed->ansi_reset[i], generated &&
                (ed->ansi_set[i] || ed->ansi_set[i + LXP_ANSI_COLORS / 2]));
    }
}

static void update_warning(Editor *ed)
{
    LxpScheme s;
    LxpSchemeColors c;
    GString *text = g_string_new(NULL);
    const char *layout = gtk_editable_get_text(GTK_EDITABLE(ed->title_layout));
    const char *style = choice_get(&ed->style);

    read_form(ed, &s);
    lxp_scheme_derive(&s, &c);
    gtk_label_set_text(GTK_LABEL(ed->variant_label),
                       c.dark ? "Also make a light version" : "Also make a dark version");
    if (lxp_color_contrast(&c.fg, &c.bg) < 4.5)
        g_string_append(text, "Text is hard to read on the background. ");
    if (lxp_color_contrast(&c.fg, &c.surface) < 4.5)
        g_string_append(text, "Text is hard to read on the surface color. ");
    if (lxp_color_contrast(&c.accent_fg, &c.accent) < 3)
        g_string_append(text, "Text on the accent color is hard to read. ");
    if (lxp_color_contrast(&c.title_active_fg, &c.title_active) < 3)
        g_string_append(text, "Title bar text is hard to read. ");
    if (lxp_color_contrast(&c.menu_active_fg, &c.menu_active) < 3)
        g_string_append(text, "Text on the menu highlight is hard to read. ");
    if (*layout && !valid_title_layout(layout))
        g_string_append(text, "Title buttons: use the letters N D S L I M C, each once. ");
    if (style && g_ascii_strcasecmp(style, "Fusion") != 0 &&
        !(g_ascii_strcasecmp(style, "kvantum") == 0 && choice_get(&ed->kvantum) == NULL))
        g_string_append_printf(text, "The %s widget style uses its own colors in Qt "
                                     "applications. ", style);
    lxp_scheme_clear(&s);

    gtk_label_set_text(GTK_LABEL(ed->warning), text->str);
    gtk_widget_set_visible(ed->warning, text->len > 0);
    g_string_free(text, TRUE);
}

/* --- live preview ------------------------------------------------------- */

static gboolean refresh_preview(gpointer data)
{
    Editor *ed = data;
    g_autoptr(GError) error = NULL;
    g_autoptr(LxpTheme) loaded = NULL;

    ed->preview_timer = 0;
    show_automatic_colors(ed);
    sync_sensitivity(ed);
    update_theme(ed);
    update_warning(ed);

    /* Generate into the work dir: the preview reads the bundled themerc. */
    if (!lxp_theme_generate(ed->theme, ed->work_dir, 1280, 720, &error) ||
        !lxp_theme_save(ed->theme, ed->work_dir, &error) ||
        !(loaded = lxp_theme_load(ed->work_dir, &error))) {
        gtk_label_set_text(GTK_LABEL(ed->error), error->message);
        gtk_widget_set_visible(ed->error, TRUE);
        return G_SOURCE_REMOVE;
    }
    gtk_widget_set_visible(ed->error, FALSE);
    lxp_preview_area_set_model(ed->preview, lxp_preview_model_new(loaded));
    return G_SOURCE_REMOVE;
}

static void schedule_preview(Editor *ed)
{
    if (ed->loading)
        return;
    g_clear_handle_id(&ed->preview_timer, g_source_remove);
    ed->preview_timer = g_timeout_add(PREVIEW_DELAY_MS, refresh_preview, ed);
}

static void on_changed(GObject *object, GParamSpec *pspec, gpointer data)
{
    schedule_preview(data);
}

static void on_editable_changed(GtkEditable *editable, gpointer data)
{
    schedule_preview(data);
}

static void on_value_changed(GObject *object, gpointer data)
{
    schedule_preview(data);
}

static void on_automatic_toggled(GtkCheckButton *check, gpointer data)
{
    schedule_preview(data);
}

/* Picking an optional color means it is no longer automatic. */
static void on_optional_color(GObject *object, GParamSpec *pspec, gpointer data)
{
    Editor *ed = data;
    OptionalColor *o = g_object_get_data(object, "lxp-optional");

    if (ed->loading)
        return;
    ed->loading = TRUE;
    gtk_check_button_set_active(GTK_CHECK_BUTTON(o->automatic), FALSE);
    ed->loading = FALSE;
    schedule_preview(ed);
}

/* Picking a terminal color overrides the derived one. */
static void on_ansi_color(GObject *object, GParamSpec *pspec, gpointer data)
{
    Editor *ed = data;

    if (ed->loading)
        return;
    ed->ansi_set[GPOINTER_TO_INT(g_object_get_data(object, "lxp-ansi"))] = TRUE;
    schedule_preview(ed);
}

/* Back to the derived normal and bright color of one row. */
static void on_ansi_reset(GtkButton *button, gpointer data)
{
    Editor *ed = data;
    int row = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "lxp-ansi"));

    ed->ansi_set[row] = ed->ansi_set[row + LXP_ANSI_COLORS / 2] = FALSE;
    schedule_preview(ed);
}

/* --- actions ------------------------------------------------------------ */

static void apply_preset(GtkButton *button, gpointer data)
{
    Editor *ed = data;
    int i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "lxp-preset"));
    LxpColor c;

    ed->loading = TRUE;
    lxp_color_parse(presets[i].bg, &c);
    set_button(ed->background, &c);
    lxp_color_parse(presets[i].surface, &c);
    set_button(ed->surface, &c);
    lxp_color_parse(presets[i].fg, &c);
    set_button(ed->foreground, &c);
    lxp_color_parse(presets[i].accent, &c);
    set_button(ed->accent, &c);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ed->accent_text.automatic), TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ed->title.automatic), TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ed->panel_color.automatic), TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ed->menu_bg.automatic), TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ed->menu_highlight.automatic), TRUE);
    ed->loading = FALSE;
    schedule_preview(ed);
}

/* Put the four base colors of <s> in the form; optional ones automatic. */
static void show_scheme_colors(Editor *ed, const LxpScheme *s)
{
    ed->loading = TRUE;
    set_button(ed->background, &s->background);
    set_button(ed->surface, &s->surface);
    set_button(ed->foreground, &s->foreground);
    set_button(ed->accent, &s->accent);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ed->accent_text.automatic), TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ed->title.automatic), TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ed->panel_color.automatic), TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ed->menu_bg.automatic), TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ed->menu_highlight.automatic), TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ed->link.automatic), TRUE);
    ed->loading = FALSE;
    schedule_preview(ed);
}

static void on_from_wallpaper(GtkButton *button, gpointer data)
{
    Editor *ed = data;
    LxpScheme current, s = { 0 };

    read_form(ed, &current);
    if (lxp_palette_from_image(ed->wallpaper,
                               lxp_color_luminance(&current.background) <
                               lxp_color_luminance(&current.foreground), &s))
        show_scheme_colors(ed, &s);
    lxp_scheme_clear(&current);
}

static void on_flip(GtkButton *button, gpointer data)
{
    Editor *ed = data;
    LxpScheme s;

    read_form(ed, &s);
    lxp_palette_flip(&s);
    show_scheme_colors(ed, &s);
    lxp_scheme_clear(&s);
}

static void on_harmony(GtkButton *button, gpointer data)
{
    Editor *ed = data;
    LxpHarmony harmony = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "lxp-harmony"));
    LxpColor accent = from_button(ed->accent);
    LxpColor next = lxp_palette_harmony(&accent, harmony);

    set_button(ed->accent, &next);
}

static void load_form(Editor *ed);

static void on_surprise(GtkButton *button, gpointer data)
{
    Editor *ed = data;
    g_autoptr(GRand) rand = g_rand_new();
    g_autoptr(GPtrArray) styles = lxp_catalog_list(LXP_CATALOG_QT_STYLES);
    gboolean kvantum = g_ptr_array_find_with_equal_func(styles, "kvantum", g_str_equal, NULL);
    g_autofree char *name = lxp_surprise(ed->theme, rand, kvantum);

    /* the surprise replaces the image wallpaper and the installed choices */
    lxp_theme_remove_group(ed->theme, "Wallpaper");
    load_form(ed);
    ed->loading = TRUE;
    gtk_editable_set_text(GTK_EDITABLE(ed->name), name);
    choice_set(&ed->openbox, NULL);
    choice_set(&ed->gtk, NULL);
    choice_set(&ed->lxqt, NULL);
    choice_set(&ed->kvantum, NULL);
    ed->loading = FALSE;
    schedule_preview(ed);
}

static void on_shuffle(GtkButton *button, gpointer data)
{
    Editor *ed = data;

    ed->wallpaper_seed = (guint32)g_random_int_range(1, 1000000);
    schedule_preview(ed);
}

static void set_wallpaper(Editor *ed, const char *path)
{
    g_autofree char *base = path ? g_path_get_basename(path) : NULL;

    g_free(ed->wallpaper);
    ed->wallpaper = g_strdup(path);
    gtk_label_set_text(GTK_LABEL(ed->wallpaper_label), base ? base : "None");
}

static void wallpaper_chosen(GObject *source, GAsyncResult *res, gpointer data)
{
    Editor *ed = data;
    g_autoptr(GFile) file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, NULL);
    g_autofree char *path = file ? g_file_get_path(file) : NULL;

    if (!path)
        return;
    set_wallpaper(ed, path);
    schedule_preview(ed);
}

static void choose_wallpaper(GtkButton *button, gpointer data)
{
    Editor *ed = data;
    g_autoptr(GtkFileDialog) dialog = gtk_file_dialog_new();
    g_autoptr(GtkFileFilter) filter = gtk_file_filter_new();
    g_autoptr(GListStore) filters = g_list_store_new(GTK_TYPE_FILE_FILTER);

    gtk_file_filter_set_name(filter, "Images");
    gtk_file_filter_add_mime_type(filter, "image/*");
    g_list_store_append(filters, filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_title(dialog, "Choose a Wallpaper");
    gtk_file_dialog_open(dialog, GTK_WINDOW(ed->window), NULL, wallpaper_chosen, ed);
}

static void show_error(Editor *ed, const char *message)
{
    gtk_label_set_text(GTK_LABEL(ed->error), message);
    gtk_widget_set_visible(ed->error, TRUE);
}

static void save(GtkButton *button, gpointer data)
{
    Editor *ed = data;
    const char *name = gtk_editable_get_text(GTK_EDITABLE(ed->name));
    g_autofree char *dir = NULL;
    g_autoptr(GError) error = NULL;

    if (!*name) {
        show_error(ed, "Give the theme a name.");
        return;
    }
    update_theme(ed);

    if (ed->dir) {
        dir = g_strdup(ed->dir);
    } else {
        g_autofree char *themes = lxp_paths_themes_dir();
        g_autofree char *slug = lxp_theme_slugify(name);
        g_autofree char *manifest = NULL;

        dir = g_build_filename(themes, slug, NULL);
        manifest = g_build_filename(dir, LXP_THEME_MANIFEST, NULL);
        if (g_file_test(manifest, G_FILE_TEST_EXISTS)) {
            show_error(ed, "A theme with this name already exists.");
            return;
        }
    }

    if (!lxp_theme_bundle_wallpaper(ed->theme, dir, &error) ||
        !lxp_theme_generate(ed->theme, dir, LXP_WALLPAPER_WIDTH, LXP_WALLPAPER_HEIGHT,
                            &error) ||
        !lxp_theme_save(ed->theme, dir, &error)) {
        show_error(ed, error->message);
        return;
    }

    if (ed->on_saved)
        ed->on_saved(dir, ed->user_data);
    gtk_window_destroy(GTK_WINDOW(ed->window));
}

/* --- construction ------------------------------------------------------- */

static GtkWidget *section(const char *title)
{
    GtkWidget *label = gtk_label_new(title);

    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_widget_add_css_class(label, "heading");
    gtk_widget_set_margin_top(label, 12);
    return label;
}

static GtkWidget *new_grid(void)
{
    GtkWidget *grid = gtk_grid_new();

    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    return grid;
}

static GtkWidget *color_button(Editor *ed)
{
    GtkWidget *button = gtk_color_dialog_button_new(gtk_color_dialog_new());

    gtk_color_dialog_set_with_alpha(
        gtk_color_dialog_button_get_dialog(GTK_COLOR_DIALOG_BUTTON(button)), FALSE);
    g_signal_connect(button, "notify::rgba", G_CALLBACK(on_changed), ed);
    return button;
}

static void attach_row(GtkWidget *grid, int row, const char *label, GtkWidget *widget,
                       GtkWidget *extra)
{
    GtkWidget *l = gtk_label_new(label);

    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_widget_set_hexpand(l, TRUE);
    gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
    if (extra)
        gtk_grid_attach(GTK_GRID(grid), extra, 1, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), widget, 2, row, 1, 1);
}

static void optional_row(Editor *ed, GtkWidget *grid, int row, const char *label,
                         OptionalColor *o)
{
    o->automatic = gtk_check_button_new_with_label("Automatic");
    o->button = gtk_color_dialog_button_new(gtk_color_dialog_new());
    gtk_color_dialog_set_with_alpha(
        gtk_color_dialog_button_get_dialog(GTK_COLOR_DIALOG_BUTTON(o->button)), FALSE);
    g_object_set_data(G_OBJECT(o->button), "lxp-optional", o);
    g_signal_connect(o->button, "notify::rgba", G_CALLBACK(on_optional_color), ed);
    g_signal_connect(o->automatic, "toggled", G_CALLBACK(on_automatic_toggled), ed);
    attach_row(grid, row, label, o->button, o->automatic);
}

static void optional_font_row(Editor *ed, GtkWidget *grid, int row, const char *label,
                              const char *automatic, OptionalFont *o)
{
    o->automatic = gtk_check_button_new_with_label(automatic);
    o->button = gtk_font_dialog_button_new(gtk_font_dialog_new());
    gtk_font_dialog_button_set_level(GTK_FONT_DIALOG_BUTTON(o->button), GTK_FONT_LEVEL_FONT);
    g_signal_connect(o->button, "notify::font-desc", G_CALLBACK(on_changed), ed);
    g_signal_connect(o->automatic, "toggled", G_CALLBACK(on_automatic_toggled), ed);
    attach_row(grid, row, label, o->button, o->automatic);
}

static GtkWidget *spin(Editor *ed, double min, double max, double step, const char *tooltip)
{
    GtkWidget *s = gtk_spin_button_new_with_range(min, max, step);

    if (tooltip)
        gtk_widget_set_tooltip_text(s, tooltip);
    g_signal_connect(s, "value-changed", G_CALLBACK(on_value_changed), ed);
    return s;
}

static GtkWidget *toggle(Editor *ed, const char *tooltip)
{
    GtkWidget *s = gtk_switch_new();

    gtk_widget_set_halign(s, GTK_ALIGN_END);
    gtk_widget_set_valign(s, GTK_ALIGN_CENTER);
    if (tooltip)
        gtk_widget_set_tooltip_text(s, tooltip);
    g_signal_connect(s, "notify::active", G_CALLBACK(on_changed), ed);
    return s;
}

/* A notebook page: a scrollable column of widgets. */
static GtkWidget *page(GtkWidget *notebook, const char *title)
{
    GtkWidget *scroller = gtk_scrolled_window_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), box);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scroller, gtk_label_new(title));
    return box;
}

/* QTerminal's ANSI colors: derived from the scheme, each one can be picked. */
static void build_terminal_section(Editor *ed, GtkWidget *box)
{
    GtkWidget *keep = new_grid();
    GtkWidget *grid = new_grid();
    GtkWidget *note = gtk_label_new("For QTerminal; new terminal windows use them. "
                                    "Colors you pick replace the ones made from the "
                                    "theme colors.");
    const char *headers[] = { "Normal", "Bright" };

    gtk_box_append(GTK_BOX(box), section("Terminal"));
    gtk_label_set_wrap(GTK_LABEL(note), TRUE);
    gtk_label_set_xalign(GTK_LABEL(note), 0);
    gtk_widget_add_css_class(note, "dim-label");
    gtk_box_append(GTK_BOX(box), note);

    ed->keep_terminal = toggle(ed, "Do not make terminal colors from this theme; "
                                   "the terminal keeps its color scheme");
    attach_row(keep, 0, "Keep current colors", ed->keep_terminal, NULL);
    gtk_box_append(GTK_BOX(box), keep);

    for (int col = 0; col < 2; col++) {
        GtkWidget *label = gtk_label_new(headers[col]);
        gtk_widget_add_css_class(label, "dim-label");
        gtk_grid_attach(GTK_GRID(grid), label, 1 + col, 0, 1, 1);
    }
    for (int row = 0; row < LXP_ANSI_COLORS / 2; row++) {
        GtkWidget *label = gtk_label_new(ansi_names[row]);
        GtkWidget *reset = gtk_button_new_from_icon_name("edit-undo-symbolic");

        gtk_label_set_xalign(GTK_LABEL(label), 0);
        gtk_widget_set_hexpand(label, TRUE);
        gtk_grid_attach(GTK_GRID(grid), label, 0, row + 1, 1, 1);
        for (int col = 0; col < 2; col++) {
            int i = row + col * LXP_ANSI_COLORS / 2;
            GtkWidget *button = gtk_color_dialog_button_new(gtk_color_dialog_new());

            gtk_color_dialog_set_with_alpha(
                gtk_color_dialog_button_get_dialog(GTK_COLOR_DIALOG_BUTTON(button)), FALSE);
            g_object_set_data(G_OBJECT(button), "lxp-ansi", GINT_TO_POINTER(i));
            g_signal_connect(button, "notify::rgba", G_CALLBACK(on_ansi_color), ed);
            gtk_grid_attach(GTK_GRID(grid), button, 1 + col, row + 1, 1, 1);
            ed->ansi[i] = button;
        }
        gtk_widget_set_tooltip_text(reset, "Back to the colors made from the theme");
        gtk_widget_add_css_class(reset, "flat");
        g_object_set_data(G_OBJECT(reset), "lxp-ansi", GINT_TO_POINTER(row));
        g_signal_connect(reset, "clicked", G_CALLBACK(on_ansi_reset), ed);
        gtk_grid_attach(GTK_GRID(grid), reset, 3, row + 1, 1, 1);
        ed->ansi_reset[row] = reset;
    }
    gtk_box_append(GTK_BOX(box), grid);
}

static void build_colors_page(Editor *ed, GtkWidget *box)
{
    GtkWidget *colors = new_grid();
    GtkWidget *optional = new_grid();
    GtkWidget *preset_box = gtk_flow_box_new();

    ed->name = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ed->name), "Theme name");
    g_signal_connect(ed->name, "changed", G_CALLBACK(on_editable_changed), ed);
    gtk_box_append(GTK_BOX(box), section("Name"));
    gtk_box_append(GTK_BOX(box), ed->name);
    ed->author = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ed->author), "Author (optional)");
    g_signal_connect(ed->author, "changed", G_CALLBACK(on_editable_changed), ed);
    gtk_box_append(GTK_BOX(box), ed->author);
    ed->comment = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ed->comment), "Description (optional)");
    g_signal_connect(ed->comment, "changed", G_CALLBACK(on_editable_changed), ed);
    gtk_box_append(GTK_BOX(box), ed->comment);

    gtk_box_append(GTK_BOX(box), section("Start From"));
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(preset_box), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(preset_box), 3);
    for (gsize i = 0; i < G_N_ELEMENTS(presets); i++) {
        GtkWidget *b = gtk_button_new_with_label(presets[i].name);
        g_object_set_data(G_OBJECT(b), "lxp-preset", GINT_TO_POINTER((int)i));
        g_signal_connect(b, "clicked", G_CALLBACK(apply_preset), ed);
        gtk_flow_box_append(GTK_FLOW_BOX(preset_box), b);
    }
    gtk_box_append(GTK_BOX(box), preset_box);
    {
        GtkWidget *tools = gtk_flow_box_new();
        struct { const char *label, *tip; GCallback cb; int harmony; } items[] = {
            { "From wallpaper", "Colors taken from the wallpaper image", G_CALLBACK(on_from_wallpaper), -1 },
            { "Light ⇄ Dark", "Turn a dark scheme light and back, keeping its hues", G_CALLBACK(on_flip), -1 },
            { "Opposite accent", "Accent with the complementary hue", G_CALLBACK(on_harmony), LXP_HARMONY_COMPLEMENTARY },
            { "Nearby accent", "Accent with a neighbouring hue", G_CALLBACK(on_harmony), LXP_HARMONY_ANALOGOUS },
            { "Surprise me", "A random, harmonious design: colors, windows, panel, "
                             "wallpaper and effects", G_CALLBACK(on_surprise), -1 },
        };

        gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(tools), GTK_SELECTION_NONE);
        gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(tools), 2);
        for (gsize i = 0; i < G_N_ELEMENTS(items); i++) {
            GtkWidget *b = gtk_button_new_with_label(items[i].label);
            gtk_widget_set_tooltip_text(b, items[i].tip);
            if (items[i].harmony >= 0)
                g_object_set_data(G_OBJECT(b), "lxp-harmony", GINT_TO_POINTER(items[i].harmony));
            g_signal_connect(b, "clicked", items[i].cb, ed);
            gtk_flow_box_append(GTK_FLOW_BOX(tools), b);
            if (i == 0)
                ed->from_wallpaper = b;
        }
        gtk_box_append(GTK_BOX(box), tools);
    }

    gtk_box_append(GTK_BOX(box), section("Colors"));
    ed->background = color_button(ed);
    ed->surface = color_button(ed);
    ed->foreground = color_button(ed);
    ed->accent = color_button(ed);
    attach_row(colors, 0, "Background", ed->background, NULL);
    attach_row(colors, 1, "Lists and fields", ed->surface, NULL);
    attach_row(colors, 2, "Text", ed->foreground, NULL);
    attach_row(colors, 3, "Accent", ed->accent, NULL);
    gtk_box_append(GTK_BOX(box), colors);

    optional_row(ed, optional, 0, "Text on accent", &ed->accent_text);
    optional_row(ed, optional, 1, "Title bars", &ed->title);
    optional_row(ed, optional, 2, "Panel", &ed->panel_color);
    optional_row(ed, optional, 3, "Links", &ed->link);
    ed->variant_label = gtk_label_new("Also make a dark version");
    gtk_label_set_xalign(GTK_LABEL(ed->variant_label), 0);
    gtk_widget_set_hexpand(ed->variant_label, TRUE);
    ed->variant = toggle(ed, "The theme gets a light/dark switch; the other version uses the "
                             "same settings with flipped colors");
    gtk_grid_attach(GTK_GRID(optional), ed->variant_label, 0, 4, 2, 1);
    gtk_grid_attach(GTK_GRID(optional), ed->variant, 2, 4, 1, 1);
    gtk_box_append(GTK_BOX(box), optional);

    ed->warning = gtk_label_new(NULL);
    gtk_label_set_wrap(GTK_LABEL(ed->warning), TRUE);
    gtk_label_set_xalign(GTK_LABEL(ed->warning), 0);
    gtk_widget_add_css_class(ed->warning, "warning");
    gtk_box_append(GTK_BOX(box), ed->warning);

    build_terminal_section(ed, box);
}

static void build_appearance_page(Editor *ed, GtkWidget *box)
{
    GtkWidget *windows = new_grid();
    GtkWidget *apps = new_grid();
    GtkWidget *pointer = new_grid();

    gtk_box_append(GTK_BOX(box), section("Windows"));
    choice_init(ed, &ed->openbox, lxp_catalog_list(LXP_CATALOG_OPENBOX), TRUE);
    gtk_widget_set_tooltip_text(ed->openbox.dropdown, "Openbox title bars and borders");
    attach_row(windows, 0, "Window theme", ed->openbox.dropdown, NULL);
    ed->title_layout = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(ed->title_layout), 7);
    gtk_editable_set_width_chars(GTK_EDITABLE(ed->title_layout), 9);
    gtk_widget_set_tooltip_text(ed->title_layout,
                                "Title bar layout: N icon, L title, I minimize, M maximize, "
                                "C close, S shade, D all desktops");
    g_signal_connect(ed->title_layout, "changed", G_CALLBACK(on_editable_changed), ed);
    attach_row(windows, 1, "Title buttons", ed->title_layout, NULL);
    optional_font_row(ed, windows, 2, "Title font", "Text font, bold", &ed->title_font);
    ed->keep_border = toggle(ed, "Keep the window border when a window is maximized");
    attach_row(windows, 3, "Border when maximized", ed->keep_border, NULL);
    ed->animate_iconify = toggle(ed, "Animate windows when they are minimized");
    attach_row(windows, 4, "Minimize animation", ed->animate_iconify, NULL);
    ed->buttons = gtk_drop_down_new_from_strings(button_sets);
    gtk_widget_set_tooltip_text(ed->buttons, "Generated window theme only");
    g_signal_connect(ed->buttons, "notify::selected", G_CALLBACK(on_changed), ed);
    attach_row(windows, 5, "Buttons", ed->buttons, NULL);
    ed->title_style = gtk_drop_down_new_from_strings(title_styles);
    g_signal_connect(ed->title_style, "notify::selected", G_CALLBACK(on_changed), ed);
    attach_row(windows, 6, "Title bar", ed->title_style, NULL);
    ed->title_padding = spin(ed, 1, 16, 1, "Space above and below the title");
    attach_row(windows, 7, "Title height", ed->title_padding, NULL);
    ed->title_align = gtk_drop_down_new_from_strings(title_aligns);
    g_signal_connect(ed->title_align, "notify::selected", G_CALLBACK(on_changed), ed);
    attach_row(windows, 8, "Title alignment", ed->title_align, NULL);
    ed->border_width = spin(ed, 0, 4, 1, NULL);
    attach_row(windows, 9, "Border", ed->border_width, NULL);
    optional_row(ed, windows, 10, "Menu background", &ed->menu_bg);
    gtk_widget_set_tooltip_text(ed->menu_bg.button, "Openbox menus (generated window theme)");
    optional_row(ed, windows, 11, "Menu highlight", &ed->menu_highlight);
    gtk_widget_set_tooltip_text(ed->menu_highlight.button, "Selected item of Openbox menus "
                                                           "(generated window theme)");
    gtk_box_append(GTK_BOX(box), windows);

    gtk_box_append(GTK_BOX(box), section("Applications"));
    choice_init(ed, &ed->gtk, lxp_catalog_list(LXP_CATALOG_GTK), TRUE);
    gtk_widget_set_tooltip_text(ed->gtk.dropdown, "Theme of GTK applications");
    attach_row(apps, 0, "GTK theme", ed->gtk.dropdown, NULL);
    choice_init(ed, &ed->lxqt, lxp_catalog_list(LXP_CATALOG_LXQT), TRUE);
    gtk_widget_set_tooltip_text(ed->lxqt.dropdown, "Panel, menu, runner and notifications");
    attach_row(apps, 1, "LXQt theme", ed->lxqt.dropdown, NULL);
    choice_init(ed, &ed->style, lxp_catalog_list(LXP_CATALOG_QT_STYLES), FALSE);
    gtk_widget_set_tooltip_text(ed->style.dropdown, "Widget style of Qt applications; "
                                                    "Fusion follows the colors");
    attach_row(apps, 2, "Widget style", ed->style.dropdown, NULL);
    choice_init(ed, &ed->kvantum, lxp_catalog_list(LXP_CATALOG_KVANTUM), TRUE);
    gtk_widget_set_tooltip_text(ed->kvantum.dropdown, "Used with the kvantum widget style; "
                                                      "applications pick it up when restarted");
    attach_row(apps, 3, "Kvantum theme", ed->kvantum.dropdown, NULL);
    ed->widget_radius = spin(ed, 0, 16, 1, "Corners of buttons, fields, menus and "
                                           "selections in the generated Kvantum theme");
    attach_row(apps, 8, "Widget corners", ed->widget_radius, NULL);
    choice_init(ed, &ed->icons, lxp_icon_themes_list(), FALSE);
    attach_row(apps, 4, "Icons", ed->icons.dropdown, NULL);
    ed->font = gtk_font_dialog_button_new(gtk_font_dialog_new());
    gtk_font_dialog_button_set_level(GTK_FONT_DIALOG_BUTTON(ed->font), GTK_FONT_LEVEL_FONT);
    g_signal_connect(ed->font, "notify::font-desc", G_CALLBACK(on_changed), ed);
    attach_row(apps, 5, "Font", ed->font, NULL);
    ed->tool_buttons = gtk_drop_down_new_from_strings(tool_button_labels);
    g_signal_connect(ed->tool_buttons, "notify::selected", G_CALLBACK(on_changed), ed);
    attach_row(apps, 6, "Toolbar buttons", ed->tool_buttons, NULL);
    ed->icons_follow = toggle(ed, "Recolor symbolic icons to match the text color");
    attach_row(apps, 7, "Icons follow colors", ed->icons_follow, NULL);
    ed->folder_color = toggle(ed, "Folder icons of the chosen icon theme in the accent color "
                                  "(SVG icon themes such as Breeze or Papirus)");
    attach_row(apps, 9, "Folders in accent color", ed->folder_color, NULL);
    gtk_box_append(GTK_BOX(box), apps);

    gtk_box_append(GTK_BOX(box), section("GTK Applications"));
    {
        GtkWidget *gtk_grid = new_grid();

        choice_init_full(ed, &ed->gtk_icons, lxp_icon_themes_list(), "Same as Qt applications");
        attach_row(gtk_grid, 0, "Icons", ed->gtk_icons.dropdown, NULL);
        optional_font_row(ed, gtk_grid, 1, "Font", "Same as Qt", &ed->gtk_font);
        ed->dark_mode = gtk_drop_down_new_from_strings(dark_modes);
        gtk_widget_set_tooltip_text(ed->dark_mode, "Ask GTK applications for their light or "
                                                   "dark variant; automatic follows the colors");
        g_signal_connect(ed->dark_mode, "notify::selected", G_CALLBACK(on_changed), ed);
        attach_row(gtk_grid, 2, "Variant", ed->dark_mode, NULL);
        gtk_box_append(GTK_BOX(box), gtk_grid);
    }

    gtk_box_append(GTK_BOX(box), section("Pointer"));
    choice_init(ed, &ed->cursor, lxp_catalog_list(LXP_CATALOG_CURSORS), FALSE);
    attach_row(pointer, 0, "Cursor theme", ed->cursor.dropdown, NULL);
    ed->cursor_size = spin(ed, 16, 96, 2, "Takes full effect at the next login");
    attach_row(pointer, 1, "Cursor size", ed->cursor_size, NULL);
    gtk_box_append(GTK_BOX(box), pointer);
}

static void build_desktop_page(Editor *ed, GtkWidget *box)
{
    GtkWidget *wallpaper = new_grid();
    GtkWidget *panel = new_grid();
    GtkWidget *wallpaper_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *choose = gtk_button_new_with_label("Choose…");

    ed->wallpaper_choose = choose;

    gtk_box_append(GTK_BOX(box), section("Wallpaper"));
    ed->wallpaper_source = gtk_drop_down_new_from_strings(wallpaper_sources);
    g_signal_connect(ed->wallpaper_source, "notify::selected", G_CALLBACK(on_changed), ed);
    ed->wallpaper_shuffle = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_tooltip_text(ed->wallpaper_shuffle, "New variation");
    g_signal_connect(ed->wallpaper_shuffle, "clicked", G_CALLBACK(on_shuffle), ed);
    attach_row(wallpaper, 2, "Source", ed->wallpaper_source, ed->wallpaper_shuffle);
    ed->wallpaper_label = gtk_label_new("None");
    gtk_label_set_ellipsize(GTK_LABEL(ed->wallpaper_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(ed->wallpaper_label), 18);
    g_signal_connect(choose, "clicked", G_CALLBACK(choose_wallpaper), ed);
    gtk_box_append(GTK_BOX(wallpaper_row), ed->wallpaper_label);
    gtk_box_append(GTK_BOX(wallpaper_row), choose);
    attach_row(wallpaper, 0, "Image", wallpaper_row, NULL);
    ed->wallpaper_mode = gtk_drop_down_new_from_strings(wallpaper_modes);
    g_signal_connect(ed->wallpaper_mode, "notify::selected", G_CALLBACK(on_changed), ed);
    attach_row(wallpaper, 1, "Fit", ed->wallpaper_mode, NULL);
    gtk_box_append(GTK_BOX(box), wallpaper);

    gtk_box_append(GTK_BOX(box), section("Panel"));
    ed->panel_position = gtk_drop_down_new_from_strings(panel_positions);
    g_signal_connect(ed->panel_position, "notify::selected", G_CALLBACK(on_changed), ed);
    attach_row(panel, 0, "Position", ed->panel_position, NULL);
    ed->panel_size = spin(ed, 16, 96, 2, NULL);
    attach_row(panel, 1, "Size", ed->panel_size, NULL);
    ed->panel_opacity = spin(ed, 0, 100, 5, "Percent; 100 is fully opaque");
    attach_row(panel, 2, "Opacity (%)", ed->panel_opacity, NULL);
    ed->panel_icon_size = spin(ed, 8, 64, 2, NULL);
    attach_row(panel, 3, "Icon size", ed->panel_icon_size, NULL);
    ed->panel_rows = spin(ed, 1, 4, 1, "Rows of icons and task buttons");
    attach_row(panel, 4, "Rows", ed->panel_rows, NULL);
    ed->panel_width = spin(ed, 1, 4000, 1, NULL);
    attach_row(panel, 5, "Length", ed->panel_width, NULL);
    ed->panel_width_percent = toggle(ed, "Length in percent of the screen edge, "
                                         "otherwise in pixels");
    attach_row(panel, 6, "Length in percent", ed->panel_width_percent, NULL);
    ed->panel_alignment = gtk_drop_down_new_from_strings(panel_alignments);
    g_signal_connect(ed->panel_alignment, "notify::selected", G_CALLBACK(on_changed), ed);
    attach_row(panel, 7, "Alignment", ed->panel_alignment, NULL);
    ed->panel_autohide = toggle(ed, "Hide the panel until the pointer reaches it");
    attach_row(panel, 8, "Auto-hide", ed->panel_autohide, NULL);
    ed->panel_reserve = toggle(ed, "Keep maximized windows from covering the panel");
    attach_row(panel, 9, "Reserve space", ed->panel_reserve, NULL);
    ed->panel_style = gtk_drop_down_new_from_strings(panel_styles);
    gtk_widget_set_tooltip_text(ed->panel_style, "Task buttons of the generated LXQt theme");
    g_signal_connect(ed->panel_style, "notify::selected", G_CALLBACK(on_changed), ed);
    attach_row(panel, 10, "Task buttons", ed->panel_style, NULL);
    gtk_box_append(GTK_BOX(box), panel);
}

static void build_effects_page(Editor *ed, GtkWidget *box)
{
    GtkWidget *grid = new_grid();
    GtkWidget *note = gtk_label_new("Drawn by the picom compositor, which must be "
                                    "running. Menus and the panel stay square.");

    gtk_box_append(GTK_BOX(box), section("Windows"));
    ed->corner_radius = spin(ed, 0, 32, 1, NULL);
    attach_row(grid, 0, "Corner radius", ed->corner_radius, NULL);
    ed->shadows = toggle(ed, NULL);
    attach_row(grid, 1, "Shadows", ed->shadows, NULL);
    ed->shadow_strength = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 5);
    gtk_widget_set_size_request(ed->shadow_strength, 160, -1);
    gtk_scale_set_draw_value(GTK_SCALE(ed->shadow_strength), TRUE);
    gtk_scale_set_digits(GTK_SCALE(ed->shadow_strength), 0);
    gtk_scale_set_value_pos(GTK_SCALE(ed->shadow_strength), GTK_POS_LEFT);
    g_signal_connect(ed->shadow_strength, "value-changed", G_CALLBACK(on_value_changed), ed);
    attach_row(grid, 2, "Shadow strength", ed->shadow_strength, NULL);
    ed->shadow_radius = spin(ed, 0, 48, 1, "How soft the shadow is");
    attach_row(grid, 3, "Shadow blur", ed->shadow_radius, NULL);
    ed->shadow_offset_x = spin(ed, -48, 48, 1, "Negative values: the shadow also shows "
                                               "on the left; minus the blur centers it");
    attach_row(grid, 4, "Shadow offset X", ed->shadow_offset_x, NULL);
    ed->shadow_offset_y = spin(ed, -48, 48, 1, "Negative values: the shadow also shows "
                                               "above; minus the blur centers it");
    attach_row(grid, 5, "Shadow offset Y", ed->shadow_offset_y, NULL);
    ed->inactive_opacity = spin(ed, 50, 100, 5, "Opacity of windows without focus");
    attach_row(grid, 6, "Inactive windows (%)", ed->inactive_opacity, NULL);
    ed->fading = toggle(ed, "Fade windows in and out");
    attach_row(grid, 7, "Fading", ed->fading, NULL);
    ed->blur = toggle(ed, "Blur what is behind translucent windows and panels");
    attach_row(grid, 8, "Blur", ed->blur, NULL);
    ed->blur_strength = spin(ed, 1, 20, 1, NULL);
    attach_row(grid, 9, "Blur strength", ed->blur_strength, NULL);
    ed->animations = gtk_drop_down_new_from_strings(animation_labels);
    gtk_widget_set_tooltip_text(ed->animations, "Window open, close and resize animations "
                                                "(picom 12 or newer)");
    g_signal_connect(ed->animations, "notify::selected", G_CALLBACK(on_changed), ed);
    attach_row(grid, 10, "Animations", ed->animations, NULL);
    gtk_box_append(GTK_BOX(box), grid);

    gtk_label_set_wrap(GTK_LABEL(note), TRUE);
    gtk_label_set_xalign(GTK_LABEL(note), 0);
    gtk_widget_add_css_class(note, "dim-label");
    gtk_widget_set_margin_top(note, 12);
    gtk_box_append(GTK_BOX(box), note);
}

static GtkWidget *build_form(Editor *ed)
{
    GtkWidget *notebook = gtk_notebook_new();

    build_colors_page(ed, page(notebook, "Colors"));
    build_appearance_page(ed, page(notebook, "Appearance"));
    build_desktop_page(ed, page(notebook, "Desktop"));
    build_effects_page(ed, page(notebook, "Effects"));
    return notebook;
}

static gboolean is_generated(const char *name)
{
    return !name || g_str_has_prefix(name, "lxp-");
}

/* Fill the form from the working theme. */
static void load_form(Editor *ed)
{
    g_autoptr(LxpPreviewModel) model = lxp_preview_model_new(ed->theme);
    LxpTheme *t = ed->theme;
    g_autofree char *openbox = lxp_theme_get_string(t, "Openbox", "theme");
    g_autofree char *layout = lxp_theme_get_string(t, "Openbox", "title_layout");
    g_autofree char *gtk = lxp_theme_get_string(t, "GTK", "theme");
    g_autofree char *lxqt = lxp_theme_get_string(t, "LXQt", "theme");
    g_autofree char *style = lxp_theme_get_string(t, "LXQt", "style");
    g_autofree char *icons = lxp_theme_get_string(t, "LXQt", "icon_theme");
    g_autofree char *cursor = lxp_theme_get_string(t, "Cursor", "theme");
    g_autofree char *cursor_size = lxp_theme_get_string(t, "Cursor", "size");
    g_autofree char *mode = lxp_theme_get_string(t, "Wallpaper", "mode");
    g_autofree char *position = lxp_theme_get_string(t, "Panel", "position");
    g_autofree char *fading = lxp_theme_get_string(t, "Compositor", "fading");
    g_autofree char *animations = lxp_theme_get_string(t, "Compositor", "animations");
    g_autofree char *lines = lxp_theme_get_string(t, "Panel", "lineCount");
    g_autofree char *kvantum = lxp_theme_get_string(t, "Kvantum", "theme");
    g_autofree char *author = lxp_theme_get_string(t, LXP_THEME_GROUP, "Author");
    g_autofree char *comment = lxp_theme_get_string(t, LXP_THEME_GROUP, "Comment");
    g_autofree char *variant = lxp_theme_get_string(t, LXP_SCHEME_GROUP, "variant");
    g_autofree char *keep_border = lxp_theme_get_string(t, "Openbox", "keep_border");
    g_autofree char *iconify = lxp_theme_get_string(t, "Openbox", "animate_iconify");
    g_autofree char *tool_buttons = lxp_theme_get_string(t, "LXQt", "tool_button_style");
    g_autofree char *follow = lxp_theme_get_string(t, "LXQt", "icon_follow_color_scheme");
    g_autofree char *gtk_icons = lxp_theme_get_string(t, "GTK", "icon_theme");
    g_auto(GStrv) title_font = lxp_theme_get_string_list(t, "Openbox", "font_ActiveWindow");
    g_autofree char *hidable = lxp_theme_get_string(t, "Panel", "hidable");
    LxpScheme s;
    LxpSchemeColors c;

    ed->loading = TRUE;
    gtk_editable_set_text(GTK_EDITABLE(ed->name), ed->dir ? lxp_theme_get_name(t) : "");
    gtk_editable_set_text(GTK_EDITABLE(ed->author), ed->dir && author ? author : "");
    gtk_editable_set_text(GTK_EDITABLE(ed->comment), ed->dir && comment ? comment : "");
    gtk_switch_set_active(GTK_SWITCH(ed->variant), variant && g_str_equal(variant, "true"));

    /* The theme's own scheme, else its palette, else what the preview shows. */
    if (!lxp_scheme_load(t, &s, NULL) && !lxp_scheme_from_palette(t, &s)) {
        memset(&s, 0, sizeof(s));
        s.background = model->window;
        s.surface = model->base;
        s.foreground = model->text;
        s.accent = model->highlight;
    }
    lxp_scheme_derive(&s, &c);
    set_button(ed->background, &s.background);
    set_button(ed->surface, &s.surface);
    set_button(ed->foreground, &s.foreground);
    set_button(ed->accent, &s.accent);
    set_optional(&ed->accent_text, s.has_accent_text, &s.accent_text, &c.accent_fg);
    set_optional(&ed->title, s.has_title, &s.title, &c.title_active);
    set_optional(&ed->panel_color, s.has_panel, &s.panel, &c.panel);
    set_optional(&ed->link, s.has_link, &s.link, &c.link);
    set_optional(&ed->menu_bg, s.has_menu, &s.menu, &c.menu_bg);
    set_optional(&ed->menu_highlight, s.has_menu_highlight, &s.menu_highlight, &c.menu_active);
    for (int i = 0; i < LXP_ANSI_COLORS; i++) {
        ed->ansi_set[i] = s.has_ansi[i];
        set_button(ed->ansi[i], &c.ansi[i]);
    }
    gtk_switch_set_active(GTK_SWITCH(ed->keep_terminal), s.keep_terminal);
    {
        g_autofree char *desc = s.font ? g_strdup(s.font)
                              : g_strdup_printf("%s %g", model->font_family, model->font_size);
        PangoFontDescription *font = pango_font_description_from_string(desc);
        gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(ed->font), font);
        pango_font_description_free(font);
    }

    /*
     * A new theme generates its window, GTK and LXQt themes from the
     * colors and uses Fusion, so color changes show everywhere. An edited
     * theme keeps its choices.
     */
    choice_set(&ed->openbox, !ed->dir || is_generated(openbox) ? NULL : openbox);
    choice_set(&ed->gtk, !ed->dir || is_generated(gtk) ? NULL : gtk);
    choice_set(&ed->lxqt, !ed->dir || is_generated(lxqt) ? NULL : lxqt);
    choice_set(&ed->style, ed->dir && (s.style || style) ? (s.style ? s.style : style)
                                                        : "Fusion");
    gtk_editable_set_text(GTK_EDITABLE(ed->title_layout), layout ? layout : "NLIMC");
    /* generated unless an installed Kvantum theme is named */
    choice_set(&ed->kvantum, kvantum && !is_generated(kvantum) && ed->dir ? kvantum : NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ed->panel_style),
        string_index(panel_style_values, s.panel_style ? s.panel_style : "underline"));
    /* the scheme's radius, else the default for a generated Kvantum theme */
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ed->widget_radius),
                              s.has_widget_radius ? s.widget_radius : 6);
    /* with accent folders, the theme's icon theme is the generated overlay */
    if (s.icons_base)
        choice_set(&ed->icons, s.icons_base);
    else if (icons && !g_str_has_prefix(icons, "lxp-"))
        choice_set(&ed->icons, icons);
    gtk_switch_set_active(GTK_SWITCH(ed->folder_color), s.folder_color);
    if (cursor)
        choice_set(&ed->cursor, cursor);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ed->cursor_size),
                              cursor_size ? g_ascii_strtod(cursor_size, NULL) : 24);
    /* window title font: the scheme's, else what Openbox uses now */
    {
        g_autofree char *text_font = get_font(ed->font);
        g_autofree char *current = NULL;

        if (!s.title_font && title_font && title_font[0] && title_font[1])
            current = g_strdup_printf("%s%s%s %s", title_font[0],
                title_font[2] && g_ascii_strcasecmp(title_font[2], "bold") == 0 ? " Bold" : "",
                title_font[2] && title_font[3] &&
                    g_ascii_strcasecmp(title_font[3], "italic") == 0 ? " Italic" : "",
                title_font[1]);
        set_optional_font(&ed->title_font, s.title_font ? s.title_font : current, text_font);
        set_optional_font(&ed->gtk_font, s.gtk_font, text_font);
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ed->title_style),
                               g_strcmp0(s.title_style, "gradient") == 0 ? 1 : 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ed->title_padding),
                              s.title_padding > 0 ? s.title_padding : 4);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ed->border_width),
                              s.has_border_width ? s.border_width : 1);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ed->title_align),
                               string_index(title_align_values, s.title_align ? s.title_align : "center"));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ed->buttons),
                               string_index(button_set_values, s.buttons ? s.buttons : "classic"));
    ed->wallpaper_seed = s.wallpaper_seed ? s.wallpaper_seed : (guint32)g_random_int_range(1, 1000000);
    if (lxp_wallgen_is_style(s.wallpaper))
        gtk_drop_down_set_selected(GTK_DROP_DOWN(ed->wallpaper_source),
                                   1 + string_index(lxp_wallgen_styles, s.wallpaper));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ed->dark_mode),
        !s.prefer_dark ? 0 : g_str_equal(s.prefer_dark, "true") ? 2 : 1);
    choice_set(&ed->gtk_icons, gtk_icons && g_strcmp0(gtk_icons, icons) != 0 ? gtk_icons : NULL);
    gtk_switch_set_active(GTK_SWITCH(ed->keep_border), !keep_border ||
                          g_ascii_strcasecmp(keep_border, "no") != 0);
    gtk_switch_set_active(GTK_SWITCH(ed->animate_iconify), !iconify ||
                          g_ascii_strcasecmp(iconify, "no") != 0);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ed->tool_buttons),
        string_index(tool_button_values, tool_buttons ? tool_buttons : "ToolButtonTextBesideIcon"));
    gtk_switch_set_active(GTK_SWITCH(ed->icons_follow), !follow ||
                          g_ascii_strcasecmp(follow, "false") != 0);
    lxp_scheme_clear(&s);

    /* a generated wallpaper is not an image to keep */
    set_wallpaper(ed, gtk_drop_down_get_selected(GTK_DROP_DOWN(ed->wallpaper_source)) == 0
                      ? model->wallpaper : NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ed->wallpaper_mode),
                               string_index(wallpaper_modes, mode ? mode : "zoom"));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ed->panel_position),
                               string_index(panel_positions, position ? position : "Bottom"));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ed->panel_size), model->panel_size);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ed->panel_opacity),
                              round(model->panel_opacity * 100));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ed->panel_icon_size), model->panel_icon_size);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ed->panel_rows),
                              lines ? g_ascii_strtod(lines, NULL) : 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ed->panel_width), model->panel_width);
    gtk_switch_set_active(GTK_SWITCH(ed->panel_width_percent), model->panel_width_percent);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ed->panel_alignment),
                               (guint)(model->panel_alignment + 1));
    gtk_switch_set_active(GTK_SWITCH(ed->panel_autohide), hidable && g_str_equal(hidable, "true"));
    gtk_switch_set_active(GTK_SWITCH(ed->panel_reserve), model->panel_reserve_space);

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ed->corner_radius), model->corner_radius);
    gtk_switch_set_active(GTK_SWITCH(ed->shadows), model->shadow);
    gtk_range_set_value(GTK_RANGE(ed->shadow_strength), round(model->shadow_opacity * 100));
    gtk_switch_set_active(GTK_SWITCH(ed->fading), fading && g_str_equal(fading, "true"));
    gtk_switch_set_active(GTK_SWITCH(ed->blur), model->blur);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ed->blur_strength), model->blur_strength);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ed->animations),
                               animations ? string_index(animation_values, animations) : 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ed->shadow_radius), model->shadow_radius);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ed->shadow_offset_x), model->shadow_offset_x);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ed->shadow_offset_y), model->shadow_offset_y);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ed->inactive_opacity),
                              round(model->inactive_opacity * 100));
    sync_sensitivity(ed);
    ed->loading = FALSE;
}

void lxp_editor_open(GtkWindow *parent, const char *dir, LxpEditorSaved on_saved,
                     gpointer user_data)
{
    Editor *ed = g_new0(Editor, 1);
    g_autoptr(GError) error = NULL;
    GtkWidget *window = gtk_window_new();
    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *cancel = gtk_button_new_with_mnemonic("_Cancel");
    GtkWidget *save_button = gtk_button_new_with_mnemonic("_Save");
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *form = NULL;
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);

    ed->window = window;
    ed->dir = g_strdup(dir);
    ed->on_saved = on_saved;
    ed->user_data = user_data;
    ed->work_dir = g_dir_make_tmp("lxp-editor-XXXXXX", &error);
    ed->theme = dir ? lxp_theme_load(dir, &error)
                    : lxp_capture_current("New Theme", NULL, NULL);
    if (!ed->work_dir || !ed->theme) {
        g_warning("Cannot open the editor: %s", error ? error->message : "unknown error");
        editor_free(ed);
        gtk_window_destroy(GTK_WINDOW(window));
        return;
    }
    g_object_set_data_full(G_OBJECT(window), "lxp-editor", ed, (GDestroyNotify)editor_free);

    gtk_window_set_title(GTK_WINDOW(window), dir ? "Edit Theme" : "New Theme");
    gtk_window_set_application(GTK_WINDOW(window), gtk_window_get_application(parent));
    gtk_window_set_transient_for(GTK_WINDOW(window), parent);
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), 1220, 740);

    g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), window);
    gtk_widget_add_css_class(save_button, "suggested-action");
    g_signal_connect(save_button, "clicked", G_CALLBACK(save), ed);
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), FALSE);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), cancel);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), save_button);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    form = build_form(ed);
    gtk_widget_set_size_request(form, 380, -1);
    /* developer hook for screenshots of each tab */
    if (g_getenv("LXP_EDITOR_PAGE"))
        gtk_notebook_set_current_page(GTK_NOTEBOOK(form), atoi(g_getenv("LXP_EDITOR_PAGE")));

    ed->preview = lxp_preview_area_new(400, 250);
    gtk_widget_set_hexpand(ed->preview, TRUE);
    gtk_widget_set_vexpand(ed->preview, TRUE);
    ed->error = gtk_label_new(NULL);
    gtk_widget_add_css_class(ed->error, "error");
    gtk_label_set_wrap(GTK_LABEL(ed->error), TRUE);
    gtk_widget_set_visible(ed->error, FALSE);
    gtk_widget_set_margin_top(right, 18);
    gtk_widget_set_margin_bottom(right, 18);
    gtk_widget_set_margin_start(right, 12);
    gtk_widget_set_margin_end(right, 18);
    gtk_box_append(GTK_BOX(right), ed->preview);
    gtk_box_append(GTK_BOX(right), ed->error);

    gtk_paned_set_start_child(GTK_PANED(paned), form);
    gtk_paned_set_end_child(GTK_PANED(paned), right);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), FALSE);
    gtk_window_set_child(GTK_WINDOW(window), paned);

    load_form(ed);
    refresh_preview(ed);
    gtk_window_present(GTK_WINDOW(window));
}
