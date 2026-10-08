/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-window.h"

#include "lxp-apply.h"
#include "lxp-backend.h"
#include "lxp-editor.h"
#include "lxp-fileutil.h"
#include "lxp-pack.h"
#include "lxp-preview-render.h"
#include "lxp-paths.h"
#include "lxp-preview-area.h"
#include "lxp-runner.h"
#include "lxp-snapshot.h"

#include <stdlib.h>

#define TRY_SECONDS 15

/* A gallery entry: an installed theme, or the current desktop (dir NULL). */
typedef struct {
    char *dir;
    LxpTheme *theme;
} Entry;

struct _LxpWindow {
    GtkApplicationWindow parent;

    GtkWidget *gallery;
    GtkWidget *preview;
    GtkWidget *swatches;
    GtkWidget *title;
    GtkWidget *subtitle;
    GtkWidget *details;
    GtkWidget *apply_button;
    GtkWidget *try_button;
    GtkWidget *delete_button;
    GtkWidget *edit_button;
    GtkWidget *export_button;
    GtkWidget *parts_button;
    GtkWidget *missing;
    LxpPackFlags export_flags;
    GtkWidget *save_button;
    GtkWidget *spinner;
    GtkWidget *banner;
    GtkWidget *banner_label;

    GPtrArray *entries;
    Entry *selected;

    LxpRunner *runner;
    char *result;            /* last porcelain result: "kept", "reverted" */
    char *running_name;      /* theme being applied */
    gboolean running_try;

    GtkWidget *confirm;
    GtkWidget *confirm_label;
    guint confirm_timer;
    int remaining;
};

G_DEFINE_FINAL_TYPE(LxpWindow, lxp_window, GTK_TYPE_APPLICATION_WINDOW)

static void entry_free(Entry *e)
{
    g_free(e->dir);
    lxp_theme_free(e->theme);
    g_free(e);
}

/* --- banner ------------------------------------------------------------- */

static void show_banner(LxpWindow *win, const char *message, gboolean error)
{
    gtk_label_set_text(GTK_LABEL(win->banner_label), message);
    if (error) {
        gtk_widget_add_css_class(win->banner, "error");
        gtk_widget_remove_css_class(win->banner, "info");
    } else {
        gtk_widget_add_css_class(win->banner, "info");
        gtk_widget_remove_css_class(win->banner, "error");
    }
    gtk_revealer_set_reveal_child(GTK_REVEALER(win->banner), TRUE);
}

static void hide_banner(LxpWindow *win)
{
    gtk_revealer_set_reveal_child(GTK_REVEALER(win->banner), FALSE);
}

/* --- details pane ------------------------------------------------------- */

static void swatches_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                          gpointer user)
{
    LxpWindow *win = user;
    g_autoptr(LxpPreviewModel) m = NULL;
    const LxpColor *colors[7];
    double w;

    if (!win->selected)
        return;
    m = lxp_preview_model_new(win->selected->theme);
    colors[0] = &m->window;
    colors[1] = &m->base;
    colors[2] = &m->text;
    colors[3] = &m->highlight;
    colors[4] = &m->link;
    colors[5] = &m->active.title_bg;
    colors[6] = &m->panel_bg;
    w = (double)width / G_N_ELEMENTS(colors);

    for (gsize i = 0; i < G_N_ELEMENTS(colors); i++) {
        cairo_set_source_rgb(cr, colors[i]->r, colors[i]->g, colors[i]->b);
        cairo_rectangle(cr, i * w + 2, 0, w - 4, height);
        cairo_fill(cr);
    }
}

static char *describe(const LxpTheme *theme, const char *group)
{
    GKeyFile *kf = lxp_theme_get_keyfile(theme);
    g_autofree char *a = NULL, *b = NULL, *c = NULL;

#define GET(var, key) var = lxp_theme_get_string(theme, group, key)
    if (!g_key_file_has_group(kf, group))
        return NULL;

    if (g_str_equal(group, "LXQt")) {
        GET(a, "theme"); GET(b, "icon_theme"); GET(c, "style");
        return g_strdup_printf("%s theme · %s icons · %s style",
                               a ? a : "default", b ? b : "default", c ? c : "default");
    }
    if (g_str_equal(group, "Openbox")) {
        GET(a, "theme"); GET(b, "title_layout");
        return g_strdup_printf("%s · buttons %s", a ? a : "default", b ? b : "NLIMC");
    }
    if (g_str_equal(group, "GTK")) {
        GET(a, "theme"); GET(b, "icon_theme"); GET(c, "font");
        return g_strdup_printf("%s · %s icons · %s", a ? a : "default",
                               b ? b : "default", c ? c : "default font");
    }
    if (g_str_equal(group, "Cursor")) {
        GET(a, "theme"); GET(b, "size");
        return g_strdup_printf("%s, %s px", a ? a : "default", b ? b : "default");
    }
    if (g_str_equal(group, "Wallpaper")) {
        g_autofree char *base = NULL;
        GET(a, "file"); GET(b, "mode");
        base = a ? g_path_get_basename(a) : g_strdup("none");
        return g_strdup_printf("%s (%s)", base, b ? b : "zoom");
    }
    if (g_str_equal(group, "Panel")) {
        GET(a, "position"); GET(b, "panelSize"); GET(c, "opacity");
        return g_strdup_printf("%s, %s px, %s%% opaque", a ? a : "Bottom",
                               b ? b : "32", c ? c : "100");
    }
    if (g_str_equal(group, "Compositor")) {
        GET(a, "corner_radius"); GET(b, "shadow");
        return g_strdup_printf("%s px corners, %s", a ? a : "0",
                               b && g_str_equal(b, "true") ? "shadows" : "no shadows");
    }
#undef GET
    return NULL;
}

static void update_details(LxpWindow *win)
{
    static const char *const groups[] = {
        "LXQt", "Openbox", "GTK", "Cursor", "Wallpaper", "Panel", "Compositor",
    };
    Entry *e = win->selected;
    GtkWidget *child;
    int row = 0;
    gboolean idle = win->runner == NULL;

    while ((child = gtk_widget_get_first_child(win->details)))
        gtk_grid_remove(GTK_GRID(win->details), child);

    if (!e) {
        gtk_label_set_text(GTK_LABEL(win->title), "");
        gtk_label_set_text(GTK_LABEL(win->subtitle), "");
        lxp_preview_area_set_model(win->preview, NULL);
        return;
    }

    gtk_label_set_text(GTK_LABEL(win->title), lxp_theme_get_name(e->theme));
    if (e->dir) {
        g_autofree char *author = lxp_theme_get_string(e->theme, LXP_THEME_GROUP, "Author");
        g_autofree char *sub = author ? g_strdup_printf("by %s — %s", author, e->dir)
                                      : g_strdup(e->dir);
        gtk_label_set_text(GTK_LABEL(win->subtitle), sub);
    } else {
        gtk_label_set_text(GTK_LABEL(win->subtitle),
                           "What your desktop looks like right now");
    }
    lxp_preview_area_set_model(win->preview, lxp_preview_model_new(e->theme));
    gtk_widget_queue_draw(win->swatches);

    for (gsize i = 0; i < G_N_ELEMENTS(groups); i++) {
        g_autofree char *text = describe(e->theme, groups[i]);
        GtkWidget *key, *value;

        if (!text)
            continue;
        key = gtk_label_new(groups[i]);
        gtk_label_set_xalign(GTK_LABEL(key), 1);
        gtk_widget_add_css_class(key, "dim-label");
        value = gtk_label_new(text);
        gtk_label_set_xalign(GTK_LABEL(value), 0);
        gtk_label_set_ellipsize(GTK_LABEL(value), PANGO_ELLIPSIZE_END);
        gtk_label_set_selectable(GTK_LABEL(value), TRUE);
        gtk_grid_attach(GTK_GRID(win->details), key, 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(win->details), value, 1, row, 1, 1);
        row++;
    }

    if (e->dir) {
        g_autoptr(GPtrArray) problems = lxp_check_theme_all(e->theme, NULL);
        g_autoptr(GString) text = g_string_new(NULL);

        for (guint i = 0; i < problems->len; i++)
            g_string_append_printf(text, "%s⚠ %s", i ? "\n" : "",
                                   ((GError *)problems->pdata[i])->message);
        gtk_label_set_text(GTK_LABEL(win->missing), text->str);
        gtk_widget_set_visible(win->missing, problems->len > 0);
    } else {
        gtk_widget_set_visible(win->missing, FALSE);
    }

    gtk_widget_set_visible(win->apply_button, e->dir != NULL);
    gtk_widget_set_visible(win->parts_button, e->dir != NULL);
    gtk_widget_set_sensitive(win->parts_button, idle);
    gtk_widget_set_visible(win->try_button, e->dir != NULL);
    gtk_widget_set_visible(win->delete_button, e->dir != NULL);
    gtk_widget_set_visible(win->edit_button, e->dir != NULL);
    gtk_widget_set_visible(win->export_button, e->dir != NULL);
    gtk_widget_set_visible(win->save_button, e->dir == NULL);
    gtk_widget_set_sensitive(win->apply_button, idle);
    gtk_widget_set_sensitive(win->try_button, idle);
    gtk_widget_set_sensitive(win->delete_button, idle);
}

/* --- gallery ------------------------------------------------------------ */

static GtkWidget *make_card(Entry *e)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *area = lxp_preview_area_new(208, 130);
    GtkWidget *label = gtk_label_new(lxp_theme_get_name(e->theme));

    lxp_preview_area_set_model(area, lxp_preview_model_new(e->theme));
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 24);
    if (!e->dir)
        gtk_widget_add_css_class(label, "heading");
    gtk_box_append(GTK_BOX(box), area);
    gtk_box_append(GTK_BOX(box), label);
    gtk_widget_add_css_class(box, "theme-card");
    return box;
}

/* Rebuild the gallery; select the theme in <select_dir> (NULL: current desktop). */
static void reload_gallery(LxpWindow *win, const char *select_dir)
{
    g_autoptr(GPtrArray) names = lxp_theme_list_installed();
    g_autoptr(GPtrArray) failures = g_ptr_array_new_with_free_func((GDestroyNotify)g_error_free);
    g_autofree char *keep = g_strdup(select_dir);
    Entry *current = g_new0(Entry, 1);
    GtkFlowBoxChild *to_select = NULL;

    win->selected = NULL;
    gtk_flow_box_remove_all(GTK_FLOW_BOX(win->gallery));
    g_ptr_array_set_size(win->entries, 0);

    current->theme = lxp_capture_current("Current desktop", NULL, failures);
    g_ptr_array_add(win->entries, current);

    for (guint i = 0; i < names->len; i++) {
        g_autoptr(GError) error = NULL;
        g_autofree char *dir = lxp_theme_resolve(g_ptr_array_index(names, i), NULL);
        LxpTheme *theme = dir ? lxp_theme_load(dir, &error) : NULL;
        Entry *e;

        if (!theme) {
            g_warning("Skipping theme %s: %s", (char *)g_ptr_array_index(names, i),
                      error ? error->message : "not found");
            continue;
        }
        e = g_new0(Entry, 1);
        e->dir = g_steal_pointer(&dir);
        e->theme = theme;
        g_ptr_array_add(win->entries, e);
    }

    for (guint i = 0; i < win->entries->len; i++) {
        Entry *e = g_ptr_array_index(win->entries, i);
        GtkWidget *card = make_card(e);
        GtkFlowBoxChild *child;

        gtk_flow_box_append(GTK_FLOW_BOX(win->gallery), card);
        child = GTK_FLOW_BOX_CHILD(gtk_widget_get_parent(card));
        g_object_set_data(G_OBJECT(child), "lxp-entry", e);
        if (g_strcmp0(e->dir, keep) == 0)
            to_select = child;
    }
    if (to_select)
        gtk_flow_box_select_child(GTK_FLOW_BOX(win->gallery), to_select);
}

static void on_selection_changed(GtkFlowBox *box, gpointer user)
{
    LxpWindow *win = user;
    GList *selected = gtk_flow_box_get_selected_children(box);

    win->selected = selected ? g_object_get_data(selected->data, "lxp-entry") : NULL;
    g_list_free(selected);
    update_details(win);
}

/* --- running lxp ---------------------------------------------------------- */

static void set_busy(LxpWindow *win, gboolean busy)
{
    gtk_widget_set_visible(win->spinner, busy);
    gtk_spinner_set_spinning(GTK_SPINNER(win->spinner), busy);
    gtk_widget_action_set_enabled(GTK_WIDGET(win), "win.undo", !busy);
    gtk_widget_action_set_enabled(GTK_WIDGET(win), "win.history", !busy);
    gtk_widget_action_set_enabled(GTK_WIDGET(win), "win.reload-desktop", !busy);
    update_details(win);
}

static void close_confirm(LxpWindow *win)
{
    g_clear_handle_id(&win->confirm_timer, g_source_remove);
    if (win->confirm) {
        GtkWidget *confirm = g_steal_pointer(&win->confirm);
        gtk_window_destroy(GTK_WINDOW(confirm));
    }
}

static void answer_try(LxpWindow *win, gboolean keep)
{
    if (win->runner)
        lxp_runner_send(win->runner, keep ? "y" : "n");
    if (win->confirm) {
        gtk_label_set_text(GTK_LABEL(win->confirm_label),
                           keep ? "Keeping the changes…" : "Reverting…");
        g_clear_handle_id(&win->confirm_timer, g_source_remove);
    }
}

static void on_keep(GtkButton *button, gpointer user)   { answer_try(user, TRUE); }
static void on_revert(GtkButton *button, gpointer user) { answer_try(user, FALSE); }

static gboolean on_confirm_close(GtkWindow *window, gpointer user)
{
    answer_try(user, FALSE);
    return TRUE;  /* stays until lxp reports back */
}

static gboolean confirm_tick(gpointer user)
{
    LxpWindow *win = user;
    g_autofree char *text = NULL;

    if (--win->remaining <= 0) {
        gtk_label_set_text(GTK_LABEL(win->confirm_label), "Reverting…");
        win->confirm_timer = 0;
        return G_SOURCE_REMOVE;
    }
    text = g_strdup_printf("The previous look comes back in %d seconds "
                           "unless you keep it.", win->remaining);
    gtk_label_set_text(GTK_LABEL(win->confirm_label), text);
    return G_SOURCE_CONTINUE;
}

static void show_confirm(LxpWindow *win, int seconds)
{
    GtkWidget *window = gtk_window_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *heading = gtk_label_new("Keep this look?");
    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *revert = gtk_button_new_with_mnemonic("_Revert");
    GtkWidget *keep = gtk_button_new_with_mnemonic("_Keep Changes");

    close_confirm(win);
    win->remaining = seconds + 1;
    win->confirm = window;
    win->confirm_label = gtk_label_new(NULL);
    confirm_tick(win);

    gtk_window_set_title(GTK_WINDOW(window), "Try Theme");
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(win));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    g_signal_connect(window, "close-request", G_CALLBACK(on_confirm_close), win);

    gtk_widget_add_css_class(heading, "title-2");
    gtk_label_set_wrap(GTK_LABEL(win->confirm_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(win->confirm_label), 36);
    gtk_widget_add_css_class(keep, "suggested-action");
    g_signal_connect(revert, "clicked", G_CALLBACK(on_revert), win);
    g_signal_connect(keep, "clicked", G_CALLBACK(on_keep), win);

    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(buttons), revert);
    gtk_box_append(GTK_BOX(buttons), keep);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_box_append(GTK_BOX(box), heading);
    gtk_box_append(GTK_BOX(box), win->confirm_label);
    gtk_box_append(GTK_BOX(box), buttons);
    gtk_window_set_child(GTK_WINDOW(window), box);
    gtk_window_set_default_widget(GTK_WINDOW(window), keep);

    win->confirm_timer = g_timeout_add_seconds(1, confirm_tick, win);
    gtk_window_present(GTK_WINDOW(window));
}

static void on_runner_event(LxpRunner *runner, const char *event, const char *arg,
                            gpointer user)
{
    LxpWindow *win = user;

    if (g_str_equal(event, "confirm")) {
        show_confirm(win, atoi(arg));
    } else if (g_str_equal(event, "kept") || g_str_equal(event, "reverted")) {
        g_free(win->result);
        win->result = g_strdup(event);
    }
}

static void on_runner_done(LxpRunner *runner, const GError *error, const char *warnings,
                           gpointer user)
{
    LxpWindow *win = user;
    g_autofree char *message = NULL;
    g_autofree char *select = win->selected ? g_strdup(win->selected->dir) : NULL;

    win->runner = NULL;
    close_confirm(win);

    if (error) {
        message = g_strdup(error->message);
    } else if (g_strcmp0(win->result, "reverted") == 0) {
        message = g_strdup("The previous look was restored.");
    } else if (win->running_name) {
        message = g_strdup_printf(win->running_try ? "Kept “%s”." : "Applied “%s”.",
                                  win->running_name);
    } else {
        message = g_strdup("Done.");
    }
    if (!error && *warnings) {
        char *full = g_strdup_printf("%s\n%s", message, warnings);
        g_free(message);
        message = full;
    }
    show_banner(win, message, error != NULL);

    g_clear_pointer(&win->result, g_free);
    g_clear_pointer(&win->running_name, g_free);
    reload_gallery(win, select);
    set_busy(win, FALSE);
}

static void run_lxp(LxpWindow *win, const char *const *args, const char *theme_name,
                    gboolean try_mode)
{
    g_autoptr(GError) error = NULL;

    if (win->runner)
        return;
    hide_banner(win);
    win->runner = lxp_runner_start(args, on_runner_event, on_runner_done, win, &error);
    if (!win->runner) {
        show_banner(win, error->message, TRUE);
        return;
    }
    win->running_name = g_strdup(theme_name);
    win->running_try = try_mode;
    set_busy(win, TRUE);
}

/* Apply (or try) the selected theme; <only> limits it to these backends. */
static void start_apply_only(LxpWindow *win, gboolean try_mode, const char *only)
{
    g_autofree char *seconds = g_strdup_printf("%d", TRY_SECONDS);
    g_autoptr(GStrvBuilder) builder = g_strv_builder_new();
    g_auto(GStrv) args = NULL;

    if (!win->selected || !win->selected->dir)
        return;
    g_strv_builder_add_many(builder, "apply", win->selected->dir, "--porcelain", NULL);
    if (try_mode)
        g_strv_builder_add_many(builder, "--try", seconds, NULL);
    if (only)
        g_strv_builder_add_many(builder, "--only", only, NULL);
    args = g_strv_builder_end(builder);
    run_lxp(win, (const char *const *)args, lxp_theme_get_name(win->selected->theme), try_mode);
}

static void start_apply(LxpWindow *win, gboolean try_mode)
{
    start_apply_only(win, try_mode, NULL);
}

static void on_apply(GtkButton *b, gpointer user) { start_apply(user, FALSE); }
static void on_try(GtkButton *b, gpointer user)   { start_apply(user, TRUE); }

static void parts_run(GtkWidget *button, gboolean try_mode)
{
    GtkWidget *dialog = GTK_WIDGET(gtk_widget_get_root(button));
    LxpWindow *win = g_object_get_data(G_OBJECT(dialog), "lxp-window");
    GtkWidget *list = g_object_get_data(G_OBJECT(dialog), "lxp-parts");
    g_autoptr(GString) only = g_string_new(NULL);

    for (GtkWidget *c = gtk_widget_get_first_child(list); c; c = gtk_widget_get_next_sibling(c)) {
        if (!gtk_check_button_get_active(GTK_CHECK_BUTTON(c)))
            continue;
        if (only->len)
            g_string_append_c(only, ',');
        g_string_append(only, g_object_get_data(G_OBJECT(c), "lxp-backend"));
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
    if (only->len)
        start_apply_only(win, try_mode, only->str);
}

static void on_parts_try(GtkButton *b, gpointer user)   { parts_run(GTK_WIDGET(b), TRUE); }
static void on_parts_apply(GtkButton *b, gpointer user) { parts_run(GTK_WIDGET(b), FALSE); }

static void on_parts(GtkButton *button, gpointer user)
{
    LxpWindow *win = user;
    GtkWidget *dialog = gtk_window_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *intro = gtk_label_new("Choose which parts of the theme to apply. The other "
                                     "parts of your desktop stay as they are.");
    GtkWidget *list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *cancel = gtk_button_new_with_mnemonic("_Cancel");
    GtkWidget *try_button = gtk_button_new_with_mnemonic("_Try");
    GtkWidget *apply = gtk_button_new_with_mnemonic("_Apply");

    if (!win->selected || !win->selected->dir)
        return;
    for (const LxpBackend *const *b = lxp_backends_get_all(); *b; b++) {
        GtkWidget *check;

        if (!lxp_backend_is_available(*b) || !(*b)->apply)
            continue;
        check = gtk_check_button_new_with_label((*b)->description);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(check), TRUE);
        g_object_set_data(G_OBJECT(check), "lxp-backend", (gpointer)(*b)->id);
        gtk_box_append(GTK_BOX(list), check);
    }

    gtk_window_set_title(GTK_WINDOW(dialog), "Apply Parts");
    gtk_window_set_application(GTK_WINDOW(dialog), gtk_window_get_application(GTK_WINDOW(win)));
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    g_object_set_data(G_OBJECT(dialog), "lxp-window", win);
    g_object_set_data(G_OBJECT(dialog), "lxp-parts", list);

    gtk_label_set_wrap(GTK_LABEL(intro), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(intro), 44);
    gtk_label_set_xalign(GTK_LABEL(intro), 0);
    g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
    g_signal_connect(try_button, "clicked", G_CALLBACK(on_parts_try), NULL);
    gtk_widget_add_css_class(apply, "suggested-action");
    g_signal_connect(apply, "clicked", G_CALLBACK(on_parts_apply), NULL);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(buttons), try_button);
    gtk_box_append(GTK_BOX(buttons), apply);

    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_box_append(GTK_BOX(box), intro);
    gtk_box_append(GTK_BOX(box), list);
    gtk_box_append(GTK_BOX(box), buttons);
    gtk_window_set_child(GTK_WINDOW(dialog), box);
    gtk_window_present(GTK_WINDOW(dialog));
}

static void undo_activated(GtkWidget *widget, const char *action, GVariant *param)
{
    const char *args[] = { "rollback", NULL };
    run_lxp(LXP_WINDOW(widget), args, NULL, FALSE);
}

static void reload_desktop_activated(GtkWidget *widget, const char *action, GVariant *param)
{
    const char *args[] = { "reload", NULL };
    run_lxp(LXP_WINDOW(widget), args, NULL, FALSE);
}

static void refresh_activated(GtkWidget *widget, const char *action, GVariant *param)
{
    LxpWindow *win = LXP_WINDOW(widget);
    g_autofree char *select = win->selected ? g_strdup(win->selected->dir) : NULL;

    reload_gallery(win, select);
}

/* --- history ------------------------------------------------------------ */

static void on_restore(GtkButton *button, gpointer user)
{
    LxpWindow *win = user;
    const char *id = g_object_get_data(G_OBJECT(button), "lxp-snapshot");
    const char *args[] = { "rollback", id, NULL };

    gtk_window_destroy(GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(button))));
    run_lxp(win, args, NULL, FALSE);
}

static void history_activated(GtkWidget *widget, const char *action, GVariant *param)
{
    LxpWindow *win = LXP_WINDOW(widget);
    g_autoptr(GPtrArray) snapshots = lxp_snapshot_list();
    GtkWidget *dialog = gtk_window_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *intro = gtk_label_new("Your configuration files are backed up every time a "
                                     "theme is applied. Restoring one brings back the files "
                                     "as they were before that change.");
    GtkWidget *scroller = gtk_scrolled_window_new();
    GtkWidget *list = gtk_list_box_new();

    gtk_window_set_title(GTK_WINDOW(dialog), "History");
    gtk_window_set_application(GTK_WINDOW(dialog), gtk_window_get_application(GTK_WINDOW(win)));
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 460);

    gtk_label_set_wrap(GTK_LABEL(intro), TRUE);
    gtk_label_set_xalign(GTK_LABEL(intro), 0);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(list, "boxed-list");

    for (guint i = 0; i < snapshots->len; i++) {
        LxpSnapshotInfo *info = g_ptr_array_index(snapshots, i);
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        g_autofree char *when = info->created
            ? g_date_time_format(info->created, "%A %-d %B %Y, %H:%M:%S")
            : g_strdup(info->id);
        g_autofree char *detail = g_strdup_printf("%s · %u files",
            info->label && *info->label ? info->label : "manual backup", info->n_files);
        GtkWidget *title = gtk_label_new(when);
        GtkWidget *sub = gtk_label_new(detail);
        GtkWidget *restore = gtk_button_new_with_label("Restore");

        gtk_label_set_xalign(GTK_LABEL(title), 0);
        gtk_label_set_xalign(GTK_LABEL(sub), 0);
        gtk_label_set_ellipsize(GTK_LABEL(sub), PANGO_ELLIPSIZE_END);
        gtk_widget_add_css_class(sub, "dim-label");
        gtk_widget_set_hexpand(text, TRUE);
        gtk_box_append(GTK_BOX(text), title);
        gtk_box_append(GTK_BOX(text), sub);
        gtk_widget_set_valign(restore, GTK_ALIGN_CENTER);
        gtk_widget_set_sensitive(restore, win->runner == NULL);
        g_object_set_data_full(G_OBJECT(restore), "lxp-snapshot", g_strdup(info->id), g_free);
        g_signal_connect(restore, "clicked", G_CALLBACK(on_restore), win);
        gtk_box_append(GTK_BOX(row), text);
        gtk_box_append(GTK_BOX(row), restore);
        gtk_widget_set_margin_top(row, 6);
        gtk_widget_set_margin_bottom(row, 6);
        gtk_widget_set_margin_start(row, 12);
        gtk_widget_set_margin_end(row, 12);
        gtk_list_box_append(GTK_LIST_BOX(list), row);
    }
    if (snapshots->len == 0)
        gtk_list_box_set_placeholder(GTK_LIST_BOX(list),
                                     gtk_label_new("No backups yet."));

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), list);
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_box_append(GTK_BOX(box), intro);
    gtk_box_append(GTK_BOX(box), scroller);
    gtk_window_set_child(GTK_WINDOW(dialog), box);
    gtk_window_present(GTK_WINDOW(dialog));
}

/* --- save current desktop ------------------------------------------------ */

static void save_theme(LxpWindow *win, GtkWidget *dialog, GtkWidget *entry)
{
    const char *name = gtk_editable_get_text(GTK_EDITABLE(entry));
    g_autofree char *themes = lxp_paths_themes_dir();
    g_autofree char *slug = NULL;
    g_autofree char *dir = NULL;
    g_autofree char *manifest = NULL;
    g_autoptr(LxpTheme) theme = NULL;
    g_autoptr(GError) error = NULL;

    if (!*g_strstrip((char *)name))
        return;
    slug = lxp_theme_slugify(name);
    dir = g_build_filename(themes, slug, NULL);
    manifest = g_build_filename(dir, LXP_THEME_MANIFEST, NULL);
    if (g_file_test(manifest, G_FILE_TEST_EXISTS)) {
        show_banner(win, "A theme with this name already exists.", TRUE);
        return;
    }

    theme = lxp_capture_current(name, NULL, NULL);
    if (!lxp_theme_save(theme, dir, &error)) {
        show_banner(win, error->message, TRUE);
        return;
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
    reload_gallery(win, dir);
    show_banner(win, "Saved the current desktop as a new theme.", FALSE);
}

static void on_save_confirmed(GtkWidget *widget, gpointer user)
{
    GtkWidget *dialog = GTK_WIDGET(gtk_widget_get_root(widget));
    save_theme(user, dialog, g_object_get_data(G_OBJECT(dialog), "lxp-entry"));
}

static void save_current_activated(GtkWidget *widget, const char *action, GVariant *param)
{
    LxpWindow *win = LXP_WINDOW(widget);
    GtkWidget *dialog = gtk_window_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *label = gtk_label_new("Save the current desktop as a theme named:");
    GtkWidget *entry = gtk_entry_new();
    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *save = gtk_button_new_with_mnemonic("_Save");
    GtkWidget *cancel = gtk_button_new_with_mnemonic("_Cancel");

    gtk_window_set_title(GTK_WINDOW(dialog), "Save Theme");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    g_object_set_data(G_OBJECT(dialog), "lxp-entry", entry);

    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_editable_set_text(GTK_EDITABLE(entry), "My Theme");
    gtk_widget_add_css_class(save, "suggested-action");
    g_signal_connect(save, "clicked", G_CALLBACK(on_save_confirmed), win);
    g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), dialog);

    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(buttons), save);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), entry);
    gtk_box_append(GTK_BOX(box), buttons);
    gtk_window_set_child(GTK_WINDOW(dialog), box);
    gtk_window_set_default_widget(GTK_WINDOW(dialog), save);
    gtk_window_present(GTK_WINDOW(dialog));
    gtk_widget_grab_focus(entry);
}

/* --- editor ------------------------------------------------------------- */

static void on_editor_saved(const char *dir, gpointer user)
{
    LxpWindow *win = user;

    reload_gallery(win, dir);
    show_banner(win, "Theme saved. Use Try or Apply to see it on your desktop.", FALSE);
}

static void new_theme_activated(GtkWidget *widget, const char *action, GVariant *param)
{
    lxp_editor_open(GTK_WINDOW(widget), NULL, on_editor_saved, widget);
}

static void on_edit(GtkButton *button, gpointer user)
{
    LxpWindow *win = user;

    if (win->selected && win->selected->dir)
        lxp_editor_open(GTK_WINDOW(win), win->selected->dir, on_editor_saved, win);
}

/* --- import / export ---------------------------------------------------- */

static GListModel *pack_filters(void)
{
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_autoptr(GtkFileFilter) filter = gtk_file_filter_new();

    gtk_file_filter_set_name(filter, "Theme packs");
    gtk_file_filter_add_suffix(filter, "tar.gz");
    gtk_file_filter_add_suffix(filter, "tgz");
    gtk_file_filter_add_suffix(filter, "tar.xz");
    gtk_file_filter_add_suffix(filter, "tar.zst");
    g_list_store_append(filters, filter);
    return G_LIST_MODEL(filters);
}

static void import_chosen(GObject *source, GAsyncResult *res, gpointer user)
{
    LxpWindow *win = user;
    g_autoptr(GFile) file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, NULL);
    g_autofree char *path = file ? g_file_get_path(file) : NULL;
    g_autofree char *dir = NULL;
    g_autoptr(LxpTheme) theme = NULL;
    g_autoptr(GPtrArray) problems = NULL;
    g_autoptr(GError) error = NULL;
    GString *message;

    if (!path)
        return;
    if (!(dir = lxp_pack_import(path, LXP_PACK_DEFAULT, &error)) ||
        !(theme = lxp_theme_load(dir, &error))) {
        show_banner(win, error->message, TRUE);
        return;
    }

    reload_gallery(win, dir);
    problems = lxp_check_theme_all(theme, NULL);
    message = g_string_new(NULL);
    g_string_printf(message, "Imported “%s”.", lxp_theme_get_name(theme));
    if (problems->len > 0) {
        g_string_append(message, " Install these before applying it:");
        for (guint i = 0; i < problems->len; i++)
            g_string_append_printf(message, "\n• %s", ((GError *)problems->pdata[i])->message);
    }
    show_banner(win, message->str, problems->len > 0);
    g_string_free(message, TRUE);
}

static void import_activated(GtkWidget *widget, const char *action, GVariant *param)
{
    g_autoptr(GtkFileDialog) dialog = gtk_file_dialog_new();
    g_autoptr(GListModel) filters = pack_filters();

    gtk_file_dialog_set_title(dialog, "Import Theme");
    gtk_file_dialog_set_filters(dialog, filters);
    gtk_file_dialog_open(dialog, GTK_WINDOW(widget), NULL, import_chosen, widget);
}

static void export_chosen(GObject *source, GAsyncResult *res, gpointer user)
{
    LxpWindow *win = user;
    g_autoptr(GFile) file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), res, NULL);
    g_autofree char *path = file ? g_file_get_path(file) : NULL;
    g_autofree char *tmp = NULL;
    g_autofree char *preview = NULL;
    g_autoptr(GPtrArray) notes = g_ptr_array_new_with_free_func(g_free);
    g_autoptr(GError) error = NULL;
    g_autoptr(LxpPreviewModel) model = NULL;
    gboolean ok;

    if (!path || !win->selected || !win->selected->dir)
        return;

    model = lxp_preview_model_new(win->selected->theme);
    if ((tmp = g_dir_make_tmp("lxp-preview-XXXXXX", NULL))) {
        preview = g_build_filename(tmp, "preview.png", NULL);
        if (!lxp_preview_render_png(model, 960, 600, preview, NULL))
            g_clear_pointer(&preview, g_free);
    }
    ok = lxp_pack_export(win->selected->theme, path, preview, win->export_flags, notes, &error);
    if (tmp)
        lxp_remove_tree(tmp, NULL);

    if (!ok) {
        show_banner(win, error->message, TRUE);
    } else {
        g_autofree char *base = g_path_get_basename(path);
        g_autofree char *message = g_strdup_printf("Exported to %s.", base);
        show_banner(win, message, FALSE);
    }
}

static void export_theme(LxpWindow *win, LxpPackFlags flags)
{
    g_autoptr(GtkFileDialog) dialog = gtk_file_dialog_new();
    g_autoptr(GListModel) filters = pack_filters();
    g_autofree char *slug = NULL;
    g_autofree char *name = NULL;

    if (!win->selected || !win->selected->dir)
        return;
    slug = lxp_theme_slugify(lxp_theme_get_name(win->selected->theme));
    name = g_strconcat(slug, ".tar.gz", NULL);
    gtk_file_dialog_set_title(dialog, "Export Theme");
    gtk_file_dialog_set_filters(dialog, filters);
    gtk_file_dialog_set_initial_name(dialog, name);
    win->export_flags = flags;
    gtk_file_dialog_save(dialog, GTK_WINDOW(win), NULL, export_chosen, win);
}

static void on_export(GtkButton *button, gpointer user)
{
    export_theme(user, LXP_PACK_DEFAULT);
}

static void export_plain_activated(GtkWidget *widget, const char *action, GVariant *param)
{
    export_theme(LXP_WINDOW(widget), LXP_PACK_NO_BUNDLE);
}

/* --- delete ------------------------------------------------------------- */

static void delete_response(GObject *source, GAsyncResult *res, gpointer user)
{
    LxpWindow *win = user;
    g_autoptr(GError) error = NULL;
    g_autofree char *dir = NULL;
    int button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), res, NULL);

    if (button != 1 || !win->selected || !win->selected->dir)
        return;
    dir = g_strdup(win->selected->dir);
    if (!lxp_remove_tree(dir, &error)) {
        show_banner(win, error->message, TRUE);
        return;
    }
    reload_gallery(win, NULL);
    show_banner(win, "Theme deleted.", FALSE);
}

static void on_delete(GtkButton *button, gpointer user)
{
    LxpWindow *win = user;
    g_autoptr(GtkAlertDialog) dialog = NULL;
    const char *buttons[] = { "_Cancel", "_Delete", NULL };

    if (!win->selected || !win->selected->dir)
        return;
    dialog = gtk_alert_dialog_new("Delete “%s”?", lxp_theme_get_name(win->selected->theme));
    gtk_alert_dialog_set_detail(dialog, "The theme folder and everything in it will be "
                                        "removed. Your desktop is not changed.");
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, 0);
    gtk_alert_dialog_set_default_button(dialog, 0);
    gtk_alert_dialog_choose(dialog, GTK_WINDOW(win), NULL, delete_response, win);
}

/* --- construction ------------------------------------------------------- */

static GtkWidget *build_header(LxpWindow *win)
{
    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *create = gtk_button_new_from_icon_name("list-add-symbolic");
    GtkWidget *save = gtk_button_new_from_icon_name("document-save-as-symbolic");
    GtkWidget *undo = gtk_button_new_from_icon_name("edit-undo-symbolic");
    GtkWidget *history = gtk_button_new_from_icon_name("document-open-recent-symbolic");
    GtkWidget *menu_button = gtk_menu_button_new();
    g_autoptr(GMenu) menu = g_menu_new();

    gtk_widget_set_tooltip_text(create, "Create a theme from a few colors");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(create), "win.new-theme");
    gtk_widget_set_tooltip_text(save, "Save the current desktop as a theme");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(save), "win.save-current");
    gtk_widget_set_tooltip_text(undo, "Undo the last change to the desktop");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(undo), "win.undo");
    gtk_widget_set_tooltip_text(history, "Backups of your configuration, to restore");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(history), "win.history");

    g_menu_append(menu, "_Import Theme…", "win.import");
    g_menu_append(menu, "Export _Without Bundled Themes…", "win.export-plain");
    g_menu_append(menu, "_Refresh Themes", "win.refresh");
    g_menu_append(menu, "Re_load Desktop", "win.reload-desktop");
    g_menu_append(menu, "_About", "app.about");
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_button), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button), G_MENU_MODEL(menu));

    win->spinner = gtk_spinner_new();
    gtk_widget_set_visible(win->spinner, FALSE);

    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), create);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), save);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), undo);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), history);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_button);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), win->spinner);
    return header;
}

static GtkWidget *build_banner(LxpWindow *win)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic");

    win->banner_label = gtk_label_new(NULL);
    gtk_label_set_wrap(GTK_LABEL(win->banner_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(win->banner_label), 0);
    gtk_widget_set_hexpand(win->banner_label, TRUE);
    gtk_widget_add_css_class(close, "flat");
    g_signal_connect_swapped(close, "clicked", G_CALLBACK(hide_banner), win);
    gtk_box_append(GTK_BOX(box), win->banner_label);
    gtk_box_append(GTK_BOX(box), close);
    gtk_widget_add_css_class(box, "banner");

    win->banner = gtk_revealer_new();
    gtk_revealer_set_child(GTK_REVEALER(win->banner), box);
    return win->banner;
}

static GtkWidget *build_details(LxpWindow *win)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    win->preview = lxp_preview_area_new(640, 400);
    gtk_widget_set_vexpand(win->preview, TRUE);
    gtk_widget_set_hexpand(win->preview, TRUE);

    win->title = gtk_label_new(NULL);
    gtk_widget_add_css_class(win->title, "title-1");
    gtk_label_set_xalign(GTK_LABEL(win->title), 0);
    win->subtitle = gtk_label_new(NULL);
    gtk_widget_add_css_class(win->subtitle, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(win->subtitle), 0);
    gtk_label_set_ellipsize(GTK_LABEL(win->subtitle), PANGO_ELLIPSIZE_MIDDLE);

    win->swatches = gtk_drawing_area_new();
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(win->swatches), 18);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(win->swatches), swatches_draw, win, NULL);
    gtk_widget_set_tooltip_text(win->swatches, "Window, base, text, highlight, link, "
                                               "title bar and panel colors");

    win->missing = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(win->missing), 0);
    gtk_label_set_wrap(GTK_LABEL(win->missing), TRUE);
    gtk_widget_add_css_class(win->missing, "warning");
    gtk_widget_set_visible(win->missing, FALSE);

    win->details = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(win->details), 4);
    gtk_grid_set_column_spacing(GTK_GRID(win->details), 12);

    win->delete_button = gtk_button_new_with_mnemonic("_Delete");
    gtk_widget_add_css_class(win->delete_button, "destructive-action");
    g_signal_connect(win->delete_button, "clicked", G_CALLBACK(on_delete), win);
    win->edit_button = gtk_button_new_with_mnemonic("_Edit Colors…");
    g_signal_connect(win->edit_button, "clicked", G_CALLBACK(on_edit), win);
    win->export_button = gtk_button_new_with_mnemonic("E_xport…");
    gtk_widget_set_tooltip_text(win->export_button, "Save as a .tar.gz pack to share");
    g_signal_connect(win->export_button, "clicked", G_CALLBACK(on_export), win);
    win->save_button = gtk_button_new_with_mnemonic("_Save as Theme…");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(win->save_button), "win.save-current");
    win->parts_button = gtk_button_new_with_mnemonic("Apply _Parts…");
    gtk_widget_set_tooltip_text(win->parts_button, "Apply or try only some parts of the theme");
    g_signal_connect(win->parts_button, "clicked", G_CALLBACK(on_parts), win);
    win->try_button = gtk_button_new_with_mnemonic("_Try");
    gtk_widget_set_tooltip_text(win->try_button, "Apply now and revert automatically "
                                                 "unless confirmed");
    g_signal_connect(win->try_button, "clicked", G_CALLBACK(on_try), win);
    win->apply_button = gtk_button_new_with_mnemonic("_Apply");
    gtk_widget_add_css_class(win->apply_button, "suggested-action");
    g_signal_connect(win->apply_button, "clicked", G_CALLBACK(on_apply), win);

    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(actions), win->delete_button);
    gtk_box_append(GTK_BOX(actions), spacer);
    gtk_box_append(GTK_BOX(actions), win->export_button);
    gtk_box_append(GTK_BOX(actions), win->edit_button);
    gtk_box_append(GTK_BOX(actions), win->save_button);
    gtk_box_append(GTK_BOX(actions), win->parts_button);
    gtk_box_append(GTK_BOX(actions), win->try_button);
    gtk_box_append(GTK_BOX(actions), win->apply_button);

    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_box_append(GTK_BOX(box), win->preview);
    gtk_box_append(GTK_BOX(box), win->title);
    gtk_box_append(GTK_BOX(box), win->subtitle);
    gtk_box_append(GTK_BOX(box), win->swatches);
    gtk_box_append(GTK_BOX(box), win->details);
    gtk_box_append(GTK_BOX(box), win->missing);
    gtk_box_append(GTK_BOX(box), actions);
    return box;
}

static void lxp_window_dispose(GObject *object)
{
    LxpWindow *win = LXP_WINDOW(object);

    close_confirm(win);
    G_OBJECT_CLASS(lxp_window_parent_class)->dispose(object);
}

static void lxp_window_finalize(GObject *object)
{
    LxpWindow *win = LXP_WINDOW(object);

    g_ptr_array_unref(win->entries);
    g_free(win->result);
    g_free(win->running_name);
    G_OBJECT_CLASS(lxp_window_parent_class)->finalize(object);
}

static gboolean on_close_request(GtkWindow *window, gpointer user)
{
    LxpWindow *win = LXP_WINDOW(window);

    /* Closing during a try reverts; lxp finishes on its own either way. */
    if (win->runner)
        lxp_runner_send(win->runner, "n");
    return FALSE;
}

static void lxp_window_class_init(LxpWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->dispose = lxp_window_dispose;
    object_class->finalize = lxp_window_finalize;

    gtk_widget_class_install_action(widget_class, "win.save-current", NULL,
                                    save_current_activated);
    gtk_widget_class_install_action(widget_class, "win.new-theme", NULL, new_theme_activated);
    gtk_widget_class_install_action(widget_class, "win.import", NULL, import_activated);
    gtk_widget_class_install_action(widget_class, "win.undo", NULL, undo_activated);
    gtk_widget_class_install_action(widget_class, "win.history", NULL, history_activated);
    gtk_widget_class_install_action(widget_class, "win.export-plain", NULL,
                                    export_plain_activated);
    gtk_widget_class_install_action(widget_class, "win.reload-desktop", NULL,
                                    reload_desktop_activated);
    gtk_widget_class_install_action(widget_class, "win.refresh", NULL, refresh_activated);
    gtk_widget_class_add_binding_action(widget_class, GDK_KEY_F5, 0, "win.refresh", NULL);
    gtk_widget_class_add_binding_action(widget_class, GDK_KEY_n, GDK_CONTROL_MASK,
                                        "win.new-theme", NULL);
    gtk_widget_class_add_binding_action(widget_class, GDK_KEY_o, GDK_CONTROL_MASK,
                                        "win.import", NULL);
    gtk_widget_class_add_binding_action(widget_class, GDK_KEY_z, GDK_CONTROL_MASK,
                                        "win.undo", NULL);
}

static void lxp_window_init(LxpWindow *win)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *scroller = gtk_scrolled_window_new();

    win->entries = g_ptr_array_new_with_free_func((GDestroyNotify)entry_free);

    gtk_window_set_title(GTK_WINDOW(win), "Desktop Themes");
    gtk_window_set_default_size(GTK_WINDOW(win), 1240, 780);
    gtk_window_set_titlebar(GTK_WINDOW(win), build_header(win));
    g_signal_connect(win, "close-request", G_CALLBACK(on_close_request), NULL);

    win->gallery = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(win->gallery), GTK_SELECTION_BROWSE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(win->gallery), TRUE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(win->gallery), 2);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(win->gallery), 6);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(win->gallery), 6);
    gtk_widget_set_valign(win->gallery, GTK_ALIGN_START);
    gtk_widget_set_margin_top(win->gallery, 12);
    gtk_widget_set_margin_bottom(win->gallery, 12);
    gtk_widget_set_margin_start(win->gallery, 12);
    gtk_widget_set_margin_end(win->gallery, 12);
    g_signal_connect(win->gallery, "selected-children-changed",
                     G_CALLBACK(on_selection_changed), win);

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), win->gallery);
    gtk_widget_set_size_request(scroller, 260, -1);

    gtk_paned_set_start_child(GTK_PANED(paned), scroller);
    gtk_paned_set_end_child(GTK_PANED(paned), build_details(win));
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_position(GTK_PANED(paned), 500);
    gtk_widget_set_vexpand(paned, TRUE);

    gtk_box_append(GTK_BOX(outer), build_banner(win));
    gtk_box_append(GTK_BOX(outer), paned);
    gtk_window_set_child(GTK_WINDOW(win), outer);

    reload_gallery(win, NULL);
}

LxpWindow *lxp_window_new(GtkApplication *app)
{
    return g_object_new(LXP_TYPE_WINDOW, "application", app, NULL);
}
