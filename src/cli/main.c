/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "config.h"
#include "lxp-apply.h"
#include "lxp-backend.h"
#include "lxp-fileutil.h"
#include "lxp-pack.h"
#include "lxp-paths.h"
#include "lxp-preview-render.h"
#include "lxp-scheme.h"
#include "lxp-themegen.h"
#include "lxp-wallgen.h"
#include "lxp-snapshot.h"
#include "lxp-theme.h"

#include <glib-unix.h>
#include <string.h>
#include <locale.h>
#include <stdio.h>
#include <unistd.h>


typedef int (*CommandFunc)(int argc, char **argv);

typedef struct {
    const char *name;
    const char *args;
    const char *summary;
    CommandFunc func;
} Command;

static int fail(GError *error)
{
    g_printerr("lxp: %s\n", error->message);
    g_error_free(error);
    return 1;
}

/* Parse <entries> for a subcommand; leaves positional args in argc/argv. */
static gboolean parse_options(const char *name, const char *args,
                              const GOptionEntry *entries,
                              int *argc, char ***argv)
{
    g_autofree char *usage = g_strdup_printf("%s %s", name, args);
    g_autoptr(GOptionContext) ctx = g_option_context_new(usage);
    g_autoptr(GError) error = NULL;

    if (entries)
        g_option_context_add_main_entries(ctx, entries, NULL);
    if (!g_option_context_parse(ctx, argc, argv, &error)) {
        g_printerr("lxp: %s\n", error->message);
        return FALSE;
    }
    return TRUE;
}

/* "lxqt,openbox" -> {"lxqt", "openbox"}; validates the ids. */
static gboolean parse_only(const char *value, GStrv *out)
{
    g_auto(GStrv) ids = NULL;

    *out = NULL;
    if (!value)
        return TRUE;

    ids = g_strsplit(value, ",", -1);
    for (char **id = ids; *id; id++) {
        g_strstrip(*id);
        if (!lxp_backend_find(*id)) {
            g_printerr("lxp: unknown backend “%s” (see “lxp backends”)\n", *id);
            return FALSE;
        }
    }
    *out = g_steal_pointer(&ids);
    return TRUE;
}

/*
 * Directory to use theme <name> from. Built-in themes are read-only and
 * are generated into the cache first.
 */
static char *resolve_usable(const char *name, GError **error)
{
    g_autofree char *dir = lxp_theme_resolve(name, error);
    return dir ? lxp_theme_materialize(dir, error) : NULL;
}

static int cmd_backends(int argc, char **argv)
{
    if (!parse_options("backends", "", NULL, &argc, &argv))
        return 2;

    for (const LxpBackend *const *b = lxp_backends_get_all(); *b; b++) {
        g_autoptr(GPtrArray) files = NULL;

        g_print("%-10s %-13s %s\n", (*b)->id,
                lxp_backend_is_available(*b) ? "available" : "not installed",
                (*b)->description);
        if (!(*b)->managed_files)
            continue;
        files = (*b)->managed_files();
        for (guint i = 0; i < files->len; i++)
            g_print("%-24s %s\n", "", (char *)g_ptr_array_index(files, i));
    }
    return 0;
}

static int cmd_capture(int argc, char **argv)
{
    g_autofree char *output = NULL;
    g_autofree char *author = NULL;
    g_autofree char *only_arg = NULL;
    g_auto(GStrv) only = NULL;
    gboolean force = FALSE;
    const GOptionEntry entries[] = {
        { "output", 'o', 0, G_OPTION_ARG_FILENAME, &output,
          "Write the theme to DIR instead of the themes directory", "DIR" },
        { "only", 0, 0, G_OPTION_ARG_STRING, &only_arg,
          "Capture only these backends", "ID,…" },
        { "author", 'a', 0, G_OPTION_ARG_STRING, &author,
          "Theme author", "NAME" },
        { "force", 'f', 0, G_OPTION_ARG_NONE, &force,
          "Overwrite an existing theme", NULL },
        G_OPTION_ENTRY_NULL,
    };
    g_autoptr(LxpTheme) theme = NULL;
    g_autoptr(GPtrArray) failures = NULL;
    g_autofree char *dir = NULL;
    g_autofree char *manifest = NULL;
    GError *error = NULL;

    if (!parse_options("capture", "NAME", entries, &argc, &argv) ||
        !parse_only(only_arg, &only))
        return 2;
    if (argc != 2) {
        g_printerr("Usage: lxp capture [OPTION…] NAME\n");
        return 2;
    }

    if (output) {
        dir = g_steal_pointer(&output);
    } else {
        g_autofree char *themes = lxp_paths_themes_dir();
        g_autofree char *slug = lxp_theme_slugify(argv[1]);
        dir = g_build_filename(themes, slug, NULL);
    }
    manifest = g_build_filename(dir, LXP_THEME_MANIFEST, NULL);
    if (!force && g_file_test(manifest, G_FILE_TEST_EXISTS)) {
        g_printerr("lxp: %s already exists (use --force to overwrite)\n", dir);
        return 1;
    }

    failures = g_ptr_array_new_with_free_func((GDestroyNotify)g_error_free);
    theme = lxp_capture_current(argv[1], (const char *const *)only, failures);
    if (author)
        lxp_theme_set_string(theme, LXP_THEME_GROUP, "Author", author);
    for (guint i = 0; i < failures->len; i++)
        g_printerr("lxp: %s (skipped)\n", ((GError *)failures->pdata[i])->message);

    if (!lxp_theme_save(theme, dir, &error))
        return fail(error);
    g_print("Saved theme “%s” to %s\n", argv[1], dir);
    return 0;
}

/* Theme directory for <name>: <output> or the user themes directory. */
static char *theme_dir_for(const char *name, const char *output)
{
    g_autofree char *themes = NULL;
    g_autofree char *slug = NULL;

    if (output)
        return g_strdup(output);
    themes = lxp_paths_themes_dir();
    slug = lxp_theme_slugify(name);
    return g_build_filename(themes, slug, NULL);
}

static gboolean parse_color_option(const char *name, const char *value, LxpColor *color,
                                   gboolean *set)
{
    if (!value)
        return TRUE;
    if (!lxp_color_parse(value, color)) {
        g_printerr("lxp: invalid color for --%s: “%s”\n", name, value);
        return FALSE;
    }
    if (set)
        *set = TRUE;
    return TRUE;
}

static int cmd_new(int argc, char **argv)
{
    g_autofree char *bg = NULL, *surface = NULL, *fg = NULL, *accent = NULL;
    g_autofree char *accent_text = NULL, *title = NULL, *panel = NULL;
    g_autofree char *font = NULL, *icons = NULL, *wallpaper = NULL, *mode = NULL;
    g_autofree char *wallpaper_style = NULL;
    int seed = 0;
    g_autofree char *output = NULL;
    gboolean from_current = FALSE, force = FALSE;
    const GOptionEntry entries[] = {
        { "from-current", 'c', 0, G_OPTION_ARG_NONE, &from_current,
          "Start from the current desktop (colors default to its palette)", NULL },
        { "background", 0, 0, G_OPTION_ARG_STRING, &bg, "Window background", "COLOR" },
        { "surface", 0, 0, G_OPTION_ARG_STRING, &surface, "Views and entries", "COLOR" },
        { "foreground", 0, 0, G_OPTION_ARG_STRING, &fg, "Text", "COLOR" },
        { "accent", 0, 0, G_OPTION_ARG_STRING, &accent, "Selection and highlights", "COLOR" },
        { "accent-text", 0, 0, G_OPTION_ARG_STRING, &accent_text, "Text on accent", "COLOR" },
        { "title", 0, 0, G_OPTION_ARG_STRING, &title, "Active title bar", "COLOR" },
        { "panel", 0, 0, G_OPTION_ARG_STRING, &panel, "Panel background", "COLOR" },
        { "font", 0, 0, G_OPTION_ARG_STRING, &font, "Font, e.g. “Noto Sans 10”", "FONT" },
        { "icons", 0, 0, G_OPTION_ARG_STRING, &icons, "Icon theme", "NAME" },
        { "wallpaper", 0, 0, G_OPTION_ARG_FILENAME, &wallpaper,
          "Wallpaper (copied into the theme)", "FILE" },
        { "wallpaper-mode", 0, 0, G_OPTION_ARG_STRING, &mode,
          "zoom, fit, stretch, center, tile or color", "MODE" },
        { "wallpaper-style", 0, 0, G_OPTION_ARG_STRING, &wallpaper_style,
          "Generate the wallpaper: gradient, blobs, waves or geometric", "STYLE" },
        { "seed", 0, 0, G_OPTION_ARG_INT, &seed,
          "Variation of the generated wallpaper (default: random)", "N" },
        { "output", 'o', 0, G_OPTION_ARG_FILENAME, &output,
          "Write the theme to DIR instead of the themes directory", "DIR" },
        { "force", 'f', 0, G_OPTION_ARG_NONE, &force, "Overwrite an existing theme", NULL },
        G_OPTION_ENTRY_NULL,
    };
    g_autoptr(LxpTheme) theme = NULL;
    g_autofree char *dir = NULL;
    g_autofree char *manifest = NULL;
    LxpScheme scheme = { 0 };
    gboolean seeded;
    GError *error = NULL;
    int ret = 1;

    if (!parse_options("new", "NAME", entries, &argc, &argv))
        return 2;
    if (argc != 2) {
        g_printerr("Usage: lxp new [OPTION…] NAME\n");
        return 2;
    }

    dir = theme_dir_for(argv[1], output);
    manifest = g_build_filename(dir, LXP_THEME_MANIFEST, NULL);
    if (!force && g_file_test(manifest, G_FILE_TEST_EXISTS)) {
        g_printerr("lxp: %s already exists (use --force to overwrite)\n", dir);
        return 1;
    }

    theme = from_current ? lxp_capture_current(argv[1], NULL, NULL) : lxp_theme_new(argv[1]);
    seeded = from_current && lxp_scheme_from_palette(theme, &scheme);
    if (!seeded && (!bg || !surface || !fg || !accent)) {
        g_printerr("lxp: --background, --surface, --foreground and --accent are required%s\n",
                   from_current ? " (the current desktop has no custom palette)" : "");
        return 2;
    }
    if (!parse_color_option("background", bg, &scheme.background, NULL) ||
        !parse_color_option("surface", surface, &scheme.surface, NULL) ||
        !parse_color_option("foreground", fg, &scheme.foreground, NULL) ||
        !parse_color_option("accent", accent, &scheme.accent, NULL) ||
        !parse_color_option("accent-text", accent_text, &scheme.accent_text,
                            &scheme.has_accent_text) ||
        !parse_color_option("title", title, &scheme.title, &scheme.has_title) ||
        !parse_color_option("panel", panel, &scheme.panel, &scheme.has_panel))
        return 2;
    scheme.font = g_strdup(font);
    if (wallpaper_style) {
        if (!lxp_wallgen_is_style(wallpaper_style)) {
            g_printerr("lxp: unknown wallpaper style “%s”\n", wallpaper_style);
            lxp_scheme_clear(&scheme);
            return 2;
        }
        scheme.wallpaper = g_strdup(wallpaper_style);
        scheme.wallpaper_seed = seed > 0 ? (guint32)seed : (guint32)g_random_int_range(1, 100000);
    }
    lxp_scheme_save(&scheme, theme);

    if (icons) {
        lxp_theme_set_string(theme, "LXQt", "icon_theme", icons);
        lxp_theme_set_string(theme, "GTK", "icon_theme", icons);
    }
    if (wallpaper) {
        g_autofree char *absolute = g_canonicalize_filename(wallpaper, NULL);
        lxp_theme_set_string(theme, "Wallpaper", "file", absolute);
        lxp_theme_set_string(theme, "Wallpaper", "mode", mode ? mode : "zoom");
    }

    if (!lxp_theme_bundle_wallpaper(theme, dir, &error) ||
        !lxp_theme_generate(theme, dir, LXP_WALLPAPER_WIDTH, LXP_WALLPAPER_HEIGHT, &error) ||
        !lxp_theme_save(theme, dir, &error))
        goto out;
    g_print("Created theme “%s” in %s\n", argv[1], dir);
    ret = 0;
out:
    lxp_scheme_clear(&scheme);
    return error ? fail(error) : ret;
}

static int cmd_generate(int argc, char **argv)
{
    g_autofree char *dir = NULL;
    g_autoptr(LxpTheme) theme = NULL;
    GError *error = NULL;

    if (!parse_options("generate", "NAME|DIR", NULL, &argc, &argv))
        return 2;
    if (argc != 2) {
        g_printerr("Usage: lxp generate NAME|DIR\n");
        return 2;
    }
    if ((dir = lxp_theme_resolve(argv[1], &error)) && lxp_theme_is_builtin(dir)) {
        g_printerr("lxp: “%s” is built in and generated automatically; "
                   "copy it to change it\n", argv[1]);
        return 1;
    }
    if (!dir ||
        !(theme = lxp_theme_load(dir, &error)) ||
        !lxp_theme_generate(theme, dir, LXP_WALLPAPER_WIDTH, LXP_WALLPAPER_HEIGHT, &error) ||
        !lxp_theme_save(theme, dir, &error))
        return fail(error);
    g_print("Regenerated “%s” from its color scheme\n", lxp_theme_get_name(theme));
    return 0;
}

static int cmd_list(int argc, char **argv)
{
    g_autoptr(GPtrArray) names = NULL;

    if (!parse_options("list", "", NULL, &argc, &argv))
        return 2;

    names = lxp_theme_list_installed();
    for (guint i = 0; i < names->len; i++) {
        const char *slug = g_ptr_array_index(names, i);
        g_autofree char *dir = lxp_theme_resolve(slug, NULL);
        g_autoptr(LxpTheme) theme = dir ? lxp_theme_load(dir, NULL) : NULL;

        g_print("%-24s %s\n", slug, theme ? lxp_theme_get_name(theme) : "(invalid)");
    }

    {
        g_autoptr(GPtrArray) builtin = lxp_theme_list_builtin();
        for (guint i = 0; i < builtin->len; i++) {
            g_autofree char *slug = g_path_get_basename(builtin->pdata[i]);
            g_autoptr(LxpTheme) theme = lxp_theme_load(builtin->pdata[i], NULL);
            g_print("%-24s %s (built-in)\n", slug, theme ? lxp_theme_get_name(theme) : "(invalid)");
        }
    }
    return 0;
}

static int cmd_show(int argc, char **argv)
{
    g_autofree char *dir = NULL;
    g_autofree char *data = NULL;
    g_autoptr(LxpTheme) theme = NULL;
    GError *error = NULL;

    if (!parse_options("show", "NAME|DIR", NULL, &argc, &argv))
        return 2;
    if (argc != 2) {
        g_printerr("Usage: lxp show NAME|DIR\n");
        return 2;
    }

    if (!(dir = lxp_theme_resolve(argv[1], &error)) ||
        !(theme = lxp_theme_load(dir, &error)))
        return fail(error);

    data = lxp_theme_to_data(theme);
    g_print("# %s\n%s", dir, data);
    return 0;
}

static int cmd_preview(int argc, char **argv)
{
    g_autofree char *output = NULL;
    g_autofree char *size = NULL;
    gboolean current = FALSE;
    const GOptionEntry entries[] = {
        { "output", 'o', 0, G_OPTION_ARG_FILENAME, &output,
          "PNG file to write (default: preview.png)", "FILE" },
        { "size", 's', 0, G_OPTION_ARG_STRING, &size,
          "Image size (default: 1440x900)", "WxH" },
        { "current", 'c', 0, G_OPTION_ARG_NONE, &current,
          "Preview the current desktop instead of a theme", NULL },
        G_OPTION_ENTRY_NULL,
    };
    g_autoptr(LxpTheme) theme = NULL;
    g_autoptr(LxpPreviewModel) model = NULL;
    g_autofree char *dir = NULL;
    int width = LXP_PREVIEW_WIDTH, height = LXP_PREVIEW_HEIGHT;
    GError *error = NULL;

    if (!parse_options("preview", "NAME|DIR", entries, &argc, &argv))
        return 2;
    if ((current && argc != 1) || (!current && argc != 2) ||
        (size && (sscanf(size, "%dx%d", &width, &height) != 2 ||
                  width < 16 || height < 16 || width > 8192 || height > 8192))) {
        g_printerr("Usage: lxp preview [OPTION…] NAME|DIR\n"
                   "       lxp preview --current [OPTION…]\n");
        return 2;
    }

    if (current) {
        theme = lxp_capture_current("Current desktop", NULL, NULL);
    } else if (!(dir = resolve_usable(argv[1], &error)) ||
               !(theme = lxp_theme_load(dir, &error))) {
        return fail(error);
    }

    model = lxp_preview_model_new(theme);
    if (!lxp_preview_render_png(model, width, height,
                                output ? output : "preview.png", &error))
        return fail(error);
    g_print("Wrote %s\n", output ? output : "preview.png");
    return 0;
}

/* Print what <theme> needs that is neither installed nor bundled. */
static int report_missing(const LxpTheme *theme)
{
    g_autoptr(GPtrArray) problems = lxp_check_theme_all(theme, NULL);

    for (guint i = 0; i < problems->len; i++)
        g_print("  missing: %s\n", ((GError *)problems->pdata[i])->message);
    return (int)problems->len;
}

static int cmd_check(int argc, char **argv)
{
    g_autofree char *dir = NULL;
    g_autoptr(LxpTheme) theme = NULL;
    GError *error = NULL;

    if (!parse_options("check", "NAME|DIR", NULL, &argc, &argv))
        return 2;
    if (argc != 2) {
        g_printerr("Usage: lxp check NAME|DIR\n");
        return 2;
    }
    if (!(dir = resolve_usable(argv[1], &error)) || !(theme = lxp_theme_load(dir, &error)))
        return fail(error);
    if (report_missing(theme) > 0)
        return 1;
    g_print("“%s” can be applied: everything it uses is available.\n",
            lxp_theme_get_name(theme));
    return 0;
}

static int cmd_export(int argc, char **argv)
{
    g_autofree char *output = NULL;
    gboolean no_bundle = FALSE, no_preview = FALSE;
    const GOptionEntry entries[] = {
        { "output", 'o', 0, G_OPTION_ARG_FILENAME, &output,
          "Archive to write (default: NAME.tar.gz)", "FILE" },
        { "no-bundle", 0, 0, G_OPTION_ARG_NONE, &no_bundle,
          "Do not include the Openbox/GTK/LXQt themes it uses", NULL },
        { "no-preview", 0, 0, G_OPTION_ARG_NONE, &no_preview,
          "Do not render preview.png", NULL },
        G_OPTION_ENTRY_NULL,
    };
    g_autofree char *dir = NULL;
    g_autofree char *tmp = NULL;
    g_autofree char *preview = NULL;
    g_autofree char *archive = NULL;
    g_autoptr(LxpTheme) theme = NULL;
    g_autoptr(GPtrArray) notes = g_ptr_array_new_with_free_func(g_free);
    GError *error = NULL;
    gboolean ok;

    if (!parse_options("export", "NAME|DIR", entries, &argc, &argv))
        return 2;
    if (argc != 2) {
        g_printerr("Usage: lxp export [OPTION…] NAME|DIR\n");
        return 2;
    }
    if (!(dir = resolve_usable(argv[1], &error)) || !(theme = lxp_theme_load(dir, &error)))
        return fail(error);

    if (output) {
        archive = g_steal_pointer(&output);
    } else {
        g_autofree char *slug = lxp_theme_slugify(lxp_theme_get_name(theme));
        archive = g_strconcat(slug, ".tar.gz", NULL);
    }

    if (!no_preview) {
        g_autoptr(LxpPreviewModel) model = lxp_preview_model_new(theme);
        if (!(tmp = g_dir_make_tmp("lxp-preview-XXXXXX", &error)))
            return fail(error);
        preview = g_build_filename(tmp, "preview.png", NULL);
        if (!lxp_preview_render_png(model, 960, 600, preview, &error)) {
            lxp_remove_tree(tmp, NULL);
            return fail(error);
        }
    }

    ok = lxp_pack_export(theme, archive, preview,
                         no_bundle ? LXP_PACK_NO_BUNDLE : LXP_PACK_DEFAULT, notes, &error);
    if (tmp)
        lxp_remove_tree(tmp, NULL);
    if (!ok)
        return fail(error);
    for (guint i = 0; i < notes->len; i++)
        g_print("  %s\n", (char *)notes->pdata[i]);
    g_print("Exported “%s” to %s\n", lxp_theme_get_name(theme), archive);
    return 0;
}

static int cmd_import(int argc, char **argv)
{
    gboolean force = FALSE;
    const GOptionEntry entries[] = {
        { "force", 'f', 0, G_OPTION_ARG_NONE, &force,
          "Replace an installed theme with the same name", NULL },
        G_OPTION_ENTRY_NULL,
    };
    g_autofree char *dir = NULL;
    g_autoptr(LxpTheme) theme = NULL;
    GError *error = NULL;

    if (!parse_options("import", "FILE", entries, &argc, &argv))
        return 2;
    if (argc != 2) {
        g_printerr("Usage: lxp import [OPTION…] FILE\n");
        return 2;
    }
    if (!(dir = lxp_pack_import(argv[1], force ? LXP_PACK_FORCE : LXP_PACK_DEFAULT, &error)) ||
        !(theme = lxp_theme_load(dir, &error)))
        return fail(error);

    g_print("Imported “%s” into %s\n", lxp_theme_get_name(theme), dir);
    if (report_missing(theme) > 0)
        g_print("Install the missing items before applying this theme.\n");
    return 0;
}

static int cmd_snapshot(int argc, char **argv)
{
    g_autofree char *id = NULL;
    GError *error = NULL;

    if (!parse_options("snapshot", "[LABEL]", NULL, &argc, &argv))
        return 2;

    if (!(id = lxp_snapshot_create(argc > 1 ? argv[1] : "manual", NULL, &error)))
        return fail(error);
    if (!lxp_snapshot_prune(LXP_SNAPSHOTS_KEPT, &error))
        return fail(error);
    g_print("Created snapshot %s\n", id);
    return 0;
}

static int cmd_snapshots(int argc, char **argv)
{
    g_autoptr(GPtrArray) list = NULL;

    if (!parse_options("snapshots", "", NULL, &argc, &argv))
        return 2;

    list = lxp_snapshot_list();
    for (guint i = 0; i < list->len; i++) {
        LxpSnapshotInfo *info = g_ptr_array_index(list, i);
        g_autofree char *when = info->created
            ? g_date_time_format(info->created, "%Y-%m-%d %H:%M:%S")
            : g_strdup("?");

        g_print("%s  %s  %u files  %s\n", info->id, when, info->n_files,
                info->label ? info->label : "");
    }
    return 0;
}

static int cmd_rollback(int argc, char **argv)
{
    gboolean no_reload = FALSE;
    const GOptionEntry entries[] = {
        { "no-reload", 0, 0, G_OPTION_ARG_NONE, &no_reload,
          "Only restore files, do not notify running programs", NULL },
        G_OPTION_ENTRY_NULL,
    };
    const char *id;
    GError *error = NULL;

    if (!parse_options("rollback", "[ID]", entries, &argc, &argv))
        return 2;
    id = argc > 1 ? argv[1] : NULL;

    if (!lxp_snapshot_restore(id, &error))
        return fail(error);
    g_print("Restored snapshot %s\n", id ? id : "(latest)");
    if (!no_reload && !lxp_reload(NULL, &error)) {
        g_printerr("lxp: warning: %s\n", error->message);
        g_clear_error(&error);
    }
    return 0;
}

static int cmd_reload(int argc, char **argv)
{
    g_autofree char *only_arg = NULL;
    g_auto(GStrv) only = NULL;
    const GOptionEntry entries[] = {
        { "only", 0, 0, G_OPTION_ARG_STRING, &only_arg,
          "Reload only these backends", "ID,…" },
        G_OPTION_ENTRY_NULL,
    };
    GError *error = NULL;

    if (!parse_options("reload", "", entries, &argc, &argv) ||
        !parse_only(only_arg, &only))
        return 2;
    if (!lxp_reload((const char *const *)only, &error))
        return fail(error);
    return 0;
}

/* --- apply --try ------------------------------------------------------- */

typedef struct {
    GMainLoop *loop;
    int remaining;
    gboolean keep;
    gboolean porcelain;
} TryState;

static void try_prompt(TryState *st)
{
    if (st->porcelain)
        return;
    g_print("\rKeep this theme? [y/N] (reverting in %2d s) ", st->remaining);
    fflush(stdout);
}

static gboolean try_tick(gpointer data)
{
    TryState *st = data;

    if (--st->remaining <= 0) {
        if (!st->porcelain)
            g_print("\n");
        g_main_loop_quit(st->loop);
        return G_SOURCE_REMOVE;
    }
    try_prompt(st);
    return G_SOURCE_CONTINUE;
}

static gboolean try_input(int fd, GIOCondition cond, gpointer data)
{
    TryState *st = data;
    char buf[64] = { 0 };
    ssize_t n = read(fd, buf, sizeof(buf) - 1);

    st->keep = n > 0 && g_ascii_tolower(g_strstrip(buf)[0]) == 'y';
    g_main_loop_quit(st->loop);
    return G_SOURCE_REMOVE;
}

static gboolean try_signal(gpointer data)
{
    TryState *st = data;

    if (!st->porcelain)
        g_print("\n");
    g_main_loop_quit(st->loop);
    return G_SOURCE_REMOVE;
}

/* Ask for confirmation; TRUE when the user keeps the theme in time. */
static gboolean confirm_try(int seconds, gboolean porcelain)
{
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    TryState st = { loop, seconds, FALSE, porcelain };
    guint sources[] = {
        g_timeout_add_seconds(1, try_tick, &st),
        g_unix_fd_add(STDIN_FILENO, G_IO_IN | G_IO_HUP, try_input, &st),
        g_unix_signal_add(SIGINT, try_signal, &st),
        g_unix_signal_add(SIGTERM, try_signal, &st),
        g_unix_signal_add(SIGHUP, try_signal, &st),
    };

    if (porcelain)
        g_print("confirm %d\n", seconds);
    try_prompt(&st);
    g_main_loop_run(loop);
    /* Sources that already returned G_SOURCE_REMOVE are gone. */
    for (gsize i = 0; i < G_N_ELEMENTS(sources); i++) {
        GSource *source = g_main_context_find_source_by_id(NULL, sources[i]);
        if (source)
            g_source_destroy(source);
    }
    return st.keep;
}

static int cmd_apply(int argc, char **argv)
{
    g_autofree char *only_arg = NULL;
    g_auto(GStrv) only = NULL;
    gboolean no_reload = FALSE, force = FALSE, porcelain = FALSE;
    int try_seconds = 0;
    const GOptionEntry entries[] = {
        { "only", 0, 0, G_OPTION_ARG_STRING, &only_arg,
          "Apply only these backends", "ID,…" },
        { "try", 't', 0, G_OPTION_ARG_INT, &try_seconds,
          "Revert unless confirmed within SECONDS", "SECONDS" },
        { "no-reload", 0, 0, G_OPTION_ARG_NONE, &no_reload,
          "Only write files, do not notify running programs", NULL },
        { "force", 'f', 0, G_OPTION_ARG_NONE, &force,
          "Skip checks for missing themes and files", NULL },
        { "porcelain", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &porcelain,
          "Line-based output for frontends", NULL },
        G_OPTION_ENTRY_NULL,
    };
    g_autofree char *dir = NULL;
    g_autofree char *snapshot = NULL;
    g_autoptr(LxpTheme) theme = NULL;
    const char *const *selected;
    GError *error = NULL;

    if (!parse_options("apply", "NAME|DIR", entries, &argc, &argv) ||
        !parse_only(only_arg, &only))
        return 2;
    if (argc != 2 || try_seconds < 0) {
        g_printerr("Usage: lxp apply [OPTION…] NAME|DIR\n");
        return 2;
    }
    if (try_seconds > 0 && no_reload) {
        g_printerr("lxp: --try needs reloading; drop --no-reload\n");
        return 2;
    }
    selected = (const char *const *)only;

    if (!(dir = resolve_usable(argv[1], &error)) ||
        !(theme = lxp_theme_load(dir, &error)))
        return fail(error);

    snapshot = lxp_apply_theme(theme, selected,
                               force ? LXP_APPLY_SKIP_CHECKS : LXP_APPLY_DEFAULT,
                               &error);
    if (!snapshot)
        return fail(error);
    /*
     * --porcelain prints one event per line for frontends:
     *   applied <snapshot>, confirm <seconds>, kept, reverted
     * Errors and warnings go to stderr.
     */
    if (porcelain) {
        setvbuf(stdout, NULL, _IOLBF, 0);
        g_print("applied %s\n", snapshot);
    } else {
        g_print("Applied “%s” (previous configuration saved as snapshot %s)\n",
                lxp_theme_get_name(theme), snapshot);
    }

    if (no_reload)
        return 0;
    if (!lxp_reload(selected, &error)) {
        g_printerr("lxp: warning: %s\n", error->message);
        g_clear_error(&error);
    }

    if (try_seconds > 0) {
        if (confirm_try(try_seconds, porcelain)) {
            g_print(porcelain ? "kept\n" : "Keeping “%s”.\n", lxp_theme_get_name(theme));
            return 0;
        }
        if (!lxp_rollback(snapshot, TRUE, &error))
            return fail(error);
        g_print(porcelain ? "reverted\n" : "Reverted to the previous configuration.\n");
    }
    return 0;
}

static const Command commands[] = {
    { "backends",  "",          "List supported desktop components",          cmd_backends },
    { "capture",   "NAME",      "Save the current desktop look as a theme",   cmd_capture },
    { "new",       "NAME",      "Create a theme from a few colors",           cmd_new },
    { "generate",  "NAME|DIR",  "Regenerate a theme from its [Scheme]",       cmd_generate },
    { "list",      "",          "List installed themes",                      cmd_list },
    { "check",     "NAME|DIR",  "List what a theme needs that is missing",    cmd_check },
    { "export",    "NAME|DIR",  "Save a theme as a shareable .tar.gz pack",   cmd_export },
    { "import",    "FILE",      "Install a theme pack",                       cmd_import },
    { "show",      "NAME|DIR",  "Print a theme manifest",                     cmd_show },
    { "preview",   "NAME|DIR",  "Render a desktop mockup of a theme to PNG",  cmd_preview },
    { "apply",     "NAME|DIR",  "Apply a theme (--try N to confirm or revert)", cmd_apply },
    { "reload",    "",          "Make running programs reread their config",  cmd_reload },
    { "snapshot",  "[LABEL]",   "Back up the current configuration files",    cmd_snapshot },
    { "snapshots", "",          "List configuration snapshots",               cmd_snapshots },
    { "rollback",  "[ID]",      "Restore a snapshot (default: the latest)",   cmd_rollback },
};

static void usage(FILE *out)
{
    fprintf(out, "Usage: lxp COMMAND [OPTION…]\n\nCommands:\n");
    for (gsize i = 0; i < G_N_ELEMENTS(commands); i++) {
        g_autofree char *call = g_strdup_printf("%s %s", commands[i].name,
                                                commands[i].args);
        fprintf(out, "  %-22s %s\n", call, commands[i].summary);
    }
    fprintf(out, "\nRun “lxp COMMAND --help” for command options.\n");
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");

    if (argc < 2 || g_str_equal(argv[1], "help") ||
        g_str_equal(argv[1], "--help") || g_str_equal(argv[1], "-h")) {
        usage(argc < 2 ? stderr : stdout);
        return argc < 2 ? 2 : 0;
    }
    if (g_str_equal(argv[1], "--version")) {
        g_print("lxp %s\n", LXP_VERSION);
        return 0;
    }

    for (gsize i = 0; i < G_N_ELEMENTS(commands); i++)
        if (g_str_equal(argv[1], commands[i].name))
            return commands[i].func(argc - 1, argv + 1);

    g_printerr("lxp: unknown command “%s”\n\n", argv[1]);
    usage(stderr);
    return 2;
}
