/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "config.h"
#include "lxp-backend.h"
#include "lxp-paths.h"
#include "lxp-snapshot.h"
#include "lxp-theme.h"

#include <locale.h>
#include <stdio.h>

#define SNAPSHOTS_KEPT 20

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
    gboolean force = FALSE;
    const GOptionEntry entries[] = {
        { "output", 'o', 0, G_OPTION_ARG_FILENAME, &output,
          "Write the theme to DIR instead of the themes directory", "DIR" },
        { "author", 'a', 0, G_OPTION_ARG_STRING, &author,
          "Theme author", "NAME" },
        { "force", 'f', 0, G_OPTION_ARG_NONE, &force,
          "Overwrite an existing theme", NULL },
        G_OPTION_ENTRY_NULL,
    };
    g_autoptr(LxpTheme) theme = NULL;
    g_autofree char *dir = NULL;
    g_autofree char *manifest = NULL;
    GError *error = NULL;

    if (!parse_options("capture", "NAME", entries, &argc, &argv))
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

    theme = lxp_theme_new(argv[1]);
    if (author)
        lxp_theme_set_string(theme, LXP_THEME_GROUP, "Author", author);

    for (const LxpBackend *const *b = lxp_backends_get_all(); *b; b++) {
        if (!(*b)->capture || !lxp_backend_is_available(*b))
            continue;
        if (!(*b)->capture(theme, &error)) {
            g_printerr("lxp: %s: %s (skipped)\n", (*b)->id, error->message);
            g_clear_error(&error);
            continue;
        }
        g_print("Captured %s\n", (*b)->id);
    }

    if (!lxp_theme_save(theme, dir, &error))
        return fail(error);
    g_print("Saved theme “%s” to %s\n", argv[1], dir);
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

static int cmd_snapshot(int argc, char **argv)
{
    g_autofree char *id = NULL;
    GError *error = NULL;

    if (!parse_options("snapshot", "[LABEL]", NULL, &argc, &argv))
        return 2;

    if (!(id = lxp_snapshot_create(argc > 1 ? argv[1] : "manual", NULL, &error)))
        return fail(error);
    if (!lxp_snapshot_prune(SNAPSHOTS_KEPT, &error))
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
    const char *id;
    GError *error = NULL;

    if (!parse_options("rollback", "[ID]", NULL, &argc, &argv))
        return 2;
    id = argc > 1 ? argv[1] : NULL;

    if (!lxp_snapshot_restore(id, &error))
        return fail(error);
    g_print("Restored snapshot %s\n", id ? id : "(latest)");
    g_print("Log out and back in, or reload the affected programs, to see the change.\n");
    return 0;
}

static const Command commands[] = {
    { "backends",  "",          "List supported desktop components",          cmd_backends },
    { "capture",   "NAME",      "Save the current desktop look as a theme",   cmd_capture },
    { "list",      "",          "List installed themes",                      cmd_list },
    { "show",      "NAME|DIR",  "Print a theme manifest",                     cmd_show },
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
