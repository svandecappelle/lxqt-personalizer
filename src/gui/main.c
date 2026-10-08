/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "config.h"
#include "lxp-window.h"

static const char css[] =
    ".theme-card { padding: 6px; }\n"
    ".banner { padding: 6px 12px; }\n"
    ".banner label { font-weight: bold; }\n"
    "revealer.info .banner { background: alpha(@accent_bg_color, 0.18); }\n"
    "revealer.error .banner { background: alpha(@error_color, 0.18); }\n";

static void about_activated(GSimpleAction *action, GVariant *param, gpointer app)
{
    const char *authors[] = { "Steeve Vandecappelle", NULL };

    gtk_show_about_dialog(gtk_application_get_active_window(app),
                          "program-name", "Desktop Themes",
                          "comments", "Create, preview and apply themes for LXQt and Openbox",
                          "version", LXP_VERSION,
                          "license-type", GTK_LICENSE_GPL_2_0,
                          "authors", authors,
                          "logo-icon-name", "preferences-desktop-theme",
                          NULL);
}

static void quit_activated(GSimpleAction *action, GVariant *param, gpointer app)
{
    GtkWindow *win = gtk_application_get_active_window(app);

    /* Go through close-request so a running try is reverted. */
    if (win)
        gtk_window_close(win);
    else
        g_application_quit(app);
}

static void startup(GApplication *app, gpointer user)
{
    static const GActionEntry actions[] = {
        { .name = "about", .activate = about_activated },
        { .name = "quit", .activate = quit_activated },
    };
    const char *quit_accels[] = { "<Control>q", NULL };
    g_autoptr(GtkCssProvider) provider = gtk_css_provider_new();

    g_action_map_add_action_entries(G_ACTION_MAP(app), actions, G_N_ELEMENTS(actions), app);
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.quit", quit_accels);

    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

/*
 * Developer hook: LXP_SCREENSHOT=out.png renders the window to a PNG once
 * it is shown, then quits (for docs and visual checks). Setting
 * LXP_SCREENSHOT_ACTION=win.new-theme first opens the editor and
 * captures it instead.
 */
static gboolean screenshot_cb(gpointer data)
{
    GtkWidget *win = data;
    GtkApplication *app = gtk_window_get_application(GTK_WINDOW(win));
    const char *path = g_getenv("LXP_SCREENSHOT");
    g_autoptr(GdkPaintable) paintable = NULL;
    GtkSnapshot *snapshot = gtk_snapshot_new();
    GskRenderNode *node;
    int w, h;

    /* With LXP_SCREENSHOT_ACTION, capture the window the action opened. */
    if (g_getenv("LXP_SCREENSHOT_ACTION")) {
        for (GList *l = gtk_application_get_windows(app); l; l = l->next) {
            if (l->data != data) {
                win = l->data;
                break;
            }
        }
    }
    w = gtk_widget_get_width(win);
    h = gtk_widget_get_height(win);
    paintable = gtk_widget_paintable_new(win);

    gdk_paintable_snapshot(paintable, snapshot, w, h);
    node = gtk_snapshot_free_to_node(snapshot);
    if (node) {
        GskRenderer *renderer = gtk_native_get_renderer(GTK_NATIVE(win));
        g_autoptr(GdkTexture) texture = gsk_renderer_render_texture(
            renderer, node, &GRAPHENE_RECT_INIT(0, 0, w, h));

        if (!gdk_texture_save_to_png(texture, path))
            g_printerr("Failed to save %s\n", path);
        gsk_render_node_unref(node);
    }
    g_application_quit(G_APPLICATION(app));
    return G_SOURCE_REMOVE;
}

static void activate(GApplication *app, gpointer user)
{
    GtkWindow *win = gtk_application_get_active_window(GTK_APPLICATION(app));

    if (!win)
        win = GTK_WINDOW(lxp_window_new(GTK_APPLICATION(app)));
    gtk_window_present(win);

    if (g_getenv("LXP_SCREENSHOT")) {
        const char *action = g_getenv("LXP_SCREENSHOT_ACTION");
        if (action)
            gtk_widget_activate_action(GTK_WIDGET(win), action, NULL);
        g_timeout_add(2500, screenshot_cb, win);
    }
}

int main(int argc, char **argv)
{
    g_autoptr(GtkApplication) app = gtk_application_new(LXP_DBUS_ID, G_APPLICATION_DEFAULT_FLAGS);

    g_set_application_name("Desktop Themes");
    g_signal_connect(app, "startup", G_CALLBACK(startup), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    return g_application_run(G_APPLICATION(app), argc, argv);
}
