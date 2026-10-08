/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-editor.h"

#include "lxp-apply.h"
#include "lxp-fileutil.h"
#include "lxp-icons.h"
#include "lxp-paths.h"
#include "lxp-preview-area.h"
#include "lxp-scheme.h"

#define PREVIEW_DELAY_MS 120

static const char *const wallpaper_modes[] = {
    "zoom", "fit", "stretch", "center", "tile", "color", NULL,
};
static const char *const panel_positions[] = { "Bottom", "Top", "Left", "Right", NULL };

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

typedef struct {
    GtkWidget *window;
    char *dir;               /* theme being edited, NULL for a new one */
    char *work_dir;          /* temporary directory for the live preview */
    LxpTheme *theme;         /* working copy */
    LxpEditorSaved on_saved;
    gpointer user_data;
    guint preview_timer;
    gboolean loading;        /* suppress updates while filling the form */

    GtkWidget *name;
    GtkWidget *background, *surface, *foreground, *accent;
    OptionalColor accent_text, title, panel_color;
    GtkWidget *font;
    GtkWidget *icons;
    GPtrArray *icon_names;
    GtkWidget *wallpaper_label;
    char *wallpaper;
    GtkWidget *wallpaper_mode;
    GtkWidget *panel_position;
    GtkWidget *panel_size;
    GtkWidget *warning;
    GtkWidget *preview;
    GtkWidget *error;
} Editor;

static void editor_free(Editor *ed)
{
    g_clear_handle_id(&ed->preview_timer, g_source_remove);
    if (ed->work_dir)
        lxp_remove_tree(ed->work_dir, NULL);
    g_free(ed->work_dir);
    g_free(ed->dir);
    g_free(ed->wallpaper);
    lxp_theme_free(ed->theme);
    g_clear_pointer(&ed->icon_names, g_ptr_array_unref);
    g_free(ed);
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

static int string_index(const char *const *list, const char *value)
{
    for (int i = 0; list[i]; i++)
        if (g_strcmp0(list[i], value) == 0)
            return i;
    return 0;
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
    s->font = font ? pango_font_description_to_string(font) : NULL;
}

/* Copy the form into the working theme (everything except generation). */
static void update_theme(Editor *ed)
{
    LxpScheme s;
    const char *name = gtk_editable_get_text(GTK_EDITABLE(ed->name));
    guint icon = gtk_drop_down_get_selected(GTK_DROP_DOWN(ed->icons));
    guint mode = gtk_drop_down_get_selected(GTK_DROP_DOWN(ed->wallpaper_mode));
    guint position = gtk_drop_down_get_selected(GTK_DROP_DOWN(ed->panel_position));
    g_autofree char *size = g_strdup_printf("%d",
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ed->panel_size)));

    read_form(ed, &s);
    lxp_scheme_save(&s, ed->theme);
    lxp_scheme_clear(&s);

    lxp_theme_set_string(ed->theme, LXP_THEME_GROUP, "Name", *name ? name : "Untitled");
    if (icon != GTK_INVALID_LIST_POSITION && icon < ed->icon_names->len) {
        const char *icons = g_ptr_array_index(ed->icon_names, icon);
        lxp_theme_set_string(ed->theme, "LXQt", "icon_theme", icons);
        lxp_theme_set_string(ed->theme, "GTK", "icon_theme", icons);
    }
    lxp_theme_set_string(ed->theme, "Wallpaper", "file", ed->wallpaper);
    lxp_theme_set_string(ed->theme, "Wallpaper", "mode", wallpaper_modes[mode]);
    lxp_theme_set_string(ed->theme, "Panel", "position", panel_positions[position]);
    lxp_theme_set_string(ed->theme, "Panel", "panelSize", size);
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
    };
    gboolean was_loading = ed->loading;

    read_form(ed, &s);
    lxp_scheme_derive(&s, &c);
    ed->loading = TRUE;
    for (gsize i = 0; i < G_N_ELEMENTS(rows); i++)
        if (gtk_check_button_get_active(GTK_CHECK_BUTTON(rows[i].o->automatic)))
            set_button(rows[i].o->button, rows[i].value);
    ed->loading = was_loading;
    lxp_scheme_clear(&s);
}

static void update_warning(Editor *ed)
{
    LxpScheme s;
    LxpSchemeColors c;
    GString *text = g_string_new(NULL);

    read_form(ed, &s);
    lxp_scheme_derive(&s, &c);
    if (lxp_color_contrast(&c.fg, &c.bg) < 4.5)
        g_string_append(text, "Text is hard to read on the background. ");
    if (lxp_color_contrast(&c.fg, &c.surface) < 4.5)
        g_string_append(text, "Text is hard to read on the surface color. ");
    if (lxp_color_contrast(&c.accent_fg, &c.accent) < 3)
        g_string_append(text, "Text on the accent color is hard to read. ");
    if (lxp_color_contrast(&c.title_active_fg, &c.title_active) < 3)
        g_string_append(text, "Title bar text is hard to read. ");
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
    update_theme(ed);
    update_warning(ed);

    /* Generate into the work dir: the preview reads the bundled themerc. */
    if (!lxp_scheme_generate(ed->theme, ed->work_dir, &error) ||
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
    ed->loading = FALSE;
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
        !lxp_scheme_generate(ed->theme, dir, &error) ||
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

static GtkWidget *build_form(Editor *ed)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *colors = gtk_grid_new();
    GtkWidget *optional = gtk_grid_new();
    GtkWidget *looks = gtk_grid_new();
    GtkWidget *preset_box = gtk_flow_box_new();
    GtkWidget *wallpaper_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *choose = gtk_button_new_with_label("Choose…");
    g_autoptr(GtkStringList) icon_model = gtk_string_list_new(NULL);
    GtkWidget *grids[] = { colors, optional, looks };

    for (gsize i = 0; i < G_N_ELEMENTS(grids); i++) {
        gtk_grid_set_row_spacing(GTK_GRID(grids[i]), 6);
        gtk_grid_set_column_spacing(GTK_GRID(grids[i]), 12);
    }

    ed->name = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ed->name), "Theme name");
    g_signal_connect(ed->name, "changed", G_CALLBACK(on_editable_changed), ed);
    gtk_box_append(GTK_BOX(box), section("Name"));
    gtk_box_append(GTK_BOX(box), ed->name);

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
    gtk_box_append(GTK_BOX(box), optional);

    ed->warning = gtk_label_new(NULL);
    gtk_label_set_wrap(GTK_LABEL(ed->warning), TRUE);
    gtk_label_set_xalign(GTK_LABEL(ed->warning), 0);
    gtk_widget_add_css_class(ed->warning, "warning");
    gtk_box_append(GTK_BOX(box), ed->warning);

    gtk_box_append(GTK_BOX(box), section("Look"));
    ed->font = gtk_font_dialog_button_new(gtk_font_dialog_new());
    gtk_font_dialog_button_set_level(GTK_FONT_DIALOG_BUTTON(ed->font),
                                     GTK_FONT_LEVEL_FONT);
    g_signal_connect(ed->font, "notify::font-desc", G_CALLBACK(on_changed), ed);
    attach_row(looks, 0, "Font", ed->font, NULL);

    ed->icon_names = lxp_icon_themes_list();
    for (guint i = 0; i < ed->icon_names->len; i++)
        gtk_string_list_append(icon_model, g_ptr_array_index(ed->icon_names, i));
    ed->icons = gtk_drop_down_new(G_LIST_MODEL(g_object_ref(icon_model)), NULL);
    g_signal_connect(ed->icons, "notify::selected", G_CALLBACK(on_changed), ed);
    attach_row(looks, 1, "Icons", ed->icons, NULL);

    ed->wallpaper_label = gtk_label_new("None");
    gtk_label_set_ellipsize(GTK_LABEL(ed->wallpaper_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(ed->wallpaper_label), 18);
    g_signal_connect(choose, "clicked", G_CALLBACK(choose_wallpaper), ed);
    gtk_box_append(GTK_BOX(wallpaper_row), ed->wallpaper_label);
    gtk_box_append(GTK_BOX(wallpaper_row), choose);
    attach_row(looks, 2, "Wallpaper", wallpaper_row, NULL);

    ed->wallpaper_mode = gtk_drop_down_new_from_strings(wallpaper_modes);
    g_signal_connect(ed->wallpaper_mode, "notify::selected", G_CALLBACK(on_changed), ed);
    attach_row(looks, 3, "Wallpaper mode", ed->wallpaper_mode, NULL);

    ed->panel_position = gtk_drop_down_new_from_strings(panel_positions);
    g_signal_connect(ed->panel_position, "notify::selected", G_CALLBACK(on_changed), ed);
    attach_row(looks, 4, "Panel position", ed->panel_position, NULL);

    ed->panel_size = gtk_spin_button_new_with_range(16, 96, 2);
    g_signal_connect(ed->panel_size, "value-changed", G_CALLBACK(on_editable_changed), ed);
    attach_row(looks, 5, "Panel size", ed->panel_size, NULL);
    gtk_box_append(GTK_BOX(box), looks);

    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    return box;
}

/* Fill the form from the working theme. */
static void load_form(Editor *ed)
{
    g_autoptr(LxpPreviewModel) model = lxp_preview_model_new(ed->theme);
    g_autofree char *icons = lxp_theme_get_string(ed->theme, "LXQt", "icon_theme");
    g_autofree char *mode = lxp_theme_get_string(ed->theme, "Wallpaper", "mode");
    g_autofree char *position = lxp_theme_get_string(ed->theme, "Panel", "position");
    LxpScheme s;
    LxpSchemeColors c;
    guint index = 0;

    ed->loading = TRUE;
    gtk_editable_set_text(GTK_EDITABLE(ed->name),
                          ed->dir ? lxp_theme_get_name(ed->theme) : "");

    /* The theme's own scheme, else its palette, else what the preview shows. */
    if (!lxp_scheme_load(ed->theme, &s, NULL) && !lxp_scheme_from_palette(ed->theme, &s)) {
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
    {
        g_autofree char *desc = s.font ? g_strdup(s.font)
                              : g_strdup_printf("%s %g", model->font_family, model->font_size);
        PangoFontDescription *font = pango_font_description_from_string(desc);
        gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(ed->font), font);
        pango_font_description_free(font);
    }
    lxp_scheme_clear(&s);

    if (icons && g_ptr_array_find_with_equal_func(ed->icon_names, icons, g_str_equal, &index))
        gtk_drop_down_set_selected(GTK_DROP_DOWN(ed->icons), index);
    set_wallpaper(ed, model->wallpaper);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ed->wallpaper_mode),
                               string_index(wallpaper_modes, mode ? mode : "zoom"));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ed->panel_position),
                               string_index(panel_positions, position ? position : "Bottom"));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ed->panel_size), model->panel_size);
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
    GtkWidget *scroller = gtk_scrolled_window_new();
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
    gtk_window_set_default_size(GTK_WINDOW(window), 1180, 720);

    g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), window);
    gtk_widget_add_css_class(save_button, "suggested-action");
    g_signal_connect(save_button, "clicked", G_CALLBACK(save), ed);
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), FALSE);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), cancel);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), save_button);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), build_form(ed));
    gtk_widget_set_size_request(scroller, 340, -1);

    ed->preview = lxp_preview_area_new(720, 450);
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

    gtk_paned_set_start_child(GTK_PANED(paned), scroller);
    gtk_paned_set_end_child(GTK_PANED(paned), right);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), FALSE);
    gtk_window_set_child(GTK_WINDOW(window), paned);

    load_form(ed);
    refresh_preview(ed);
    gtk_window_present(GTK_WINDOW(window));
}
