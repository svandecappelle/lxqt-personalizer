/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-system.h"

#include "lxp-error.h"

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

gboolean lxp_run(const char *const *argv, GError **error)
{
    g_autofree char *err_out = NULL;
    int status;

    if (!g_spawn_sync(NULL, (char **)argv, NULL,
                      G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL,
                      NULL, NULL, NULL, &err_out, &status, error))
        return FALSE;

    if (!g_spawn_check_wait_status(status, NULL)) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_FAILED, "%s failed: %s",
                    argv[0], err_out && *err_out ? g_strstrip(err_out) : "error");
        return FALSE;
    }
    return TRUE;
}

static GStrv read_cmdline(const char *pid)
{
    g_autofree char *path = g_build_filename("/proc", pid, "cmdline", NULL);
    g_autofree char *data = NULL;
    g_autoptr(GStrvBuilder) builder = NULL;
    gsize len;

    if (!g_file_get_contents(path, &data, &len, NULL) || len == 0)
        return NULL;

    builder = g_strv_builder_new();
    for (gsize i = 0; i < len; i += strlen(data + i) + 1)
        g_strv_builder_add(builder, data + i);
    return g_strv_builder_end(builder);
}

GStrv lxp_process_find(const char *name)
{
    g_autoptr(GDir) dir = g_dir_open("/proc", 0, NULL);
    const char *entry;

    if (!dir)
        return NULL;
    while ((entry = g_dir_read_name(dir))) {
        g_autofree char *proc = NULL;
        g_auto(GStrv) argv = NULL;
        g_autofree char *base = NULL;
        struct stat st;

        if (!g_ascii_isdigit(entry[0]))
            continue;
        proc = g_build_filename("/proc", entry, NULL);
        if (stat(proc, &st) != 0 || st.st_uid != getuid())
            continue;
        if (!(argv = read_cmdline(entry)))
            continue;
        base = g_path_get_basename(argv[0]);
        if (g_str_equal(base, name))
            return g_steal_pointer(&argv);
    }
    return NULL;
}

char *lxp_argv_option(const char *const *argv, const char *option)
{
    g_autofree char *flag = g_strconcat("--", option, NULL);
    g_autofree char *prefix = g_strconcat(flag, "=", NULL);

    for (const char *const *a = argv; a && *a; a++) {
        if (g_str_has_prefix(*a, prefix))
            return g_strdup(*a + strlen(prefix));
        if (g_str_equal(*a, flag) && a[1])
            return g_strdup(a[1]);
    }
    return NULL;
}

static char *probe_dir(const char *base, const char *name, const char *probe)
{
    g_autofree char *dir = g_build_filename(base, name, NULL);
    g_autofree char *path = probe ? g_build_filename(dir, probe, NULL)
                                  : g_strdup(dir);

    return g_file_test(path, G_FILE_TEST_EXISTS) ? g_steal_pointer(&dir) : NULL;
}

char *lxp_find_installed(const char *kind, const char *name, const char *probe)
{
    g_autoptr(GPtrArray) bases = g_ptr_array_new_with_free_func(g_free);

    if (!name || !*name || strchr(name, G_DIR_SEPARATOR))
        return NULL;

    g_ptr_array_add(bases, g_build_filename(g_get_user_data_dir(), kind, NULL));
    if (g_str_equal(kind, "themes") || g_str_equal(kind, "icons")) {
        g_autofree char *legacy = g_strconcat(".", kind, NULL);
        g_ptr_array_add(bases, g_build_filename(g_get_home_dir(), legacy, NULL));
    }
    for (const char *const *d = g_get_system_data_dirs(); *d; d++)
        g_ptr_array_add(bases, g_build_filename(*d, kind, NULL));

    for (guint i = 0; i < bases->len; i++) {
        char *found = probe_dir(g_ptr_array_index(bases, i), name, probe);
        if (found)
            return found;
    }
    return NULL;
}
