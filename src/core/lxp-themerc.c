/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-themerc.h"

#include "lxp-error.h"
#include "lxp-system.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    GStrv components;  /* "window", "*", "label", ... */
    char *value;
} Resource;

struct _LxpThemerc {
    GPtrArray *resources;
    char *dir;
};

/* Per key component: how it was matched. Higher is more specific. */
enum { MATCH_SKIP = 1, MATCH_ANY = 2, MATCH_NAME = 3 };

static void resource_free(Resource *r)
{
    g_strfreev(r->components);
    g_free(r->value);
    g_free(r);
}

/* Split "window.*.label" / "window*label" into components; '*' is kept. */
static GStrv split_pattern(const char *pattern)
{
    g_autoptr(GStrvBuilder) builder = g_strv_builder_new();
    GString *cur = g_string_new(NULL);

    for (const char *p = pattern; ; p++) {
        if (*p == '.' || *p == '*' || *p == '\0') {
            if (cur->len)
                g_strv_builder_add(builder, cur->str);
            g_string_truncate(cur, 0);
            if (*p == '*')
                g_strv_builder_add(builder, "*");
            if (*p == '\0')
                break;
        } else if (!g_ascii_isspace(*p)) {
            g_string_append_c(cur, g_ascii_tolower(*p));
        }
    }
    g_string_free(cur, TRUE);
    return g_strv_builder_end(builder);
}

LxpThemerc *lxp_themerc_new_from_data(const char *data)
{
    LxpThemerc *rc = g_new0(LxpThemerc, 1);
    g_auto(GStrv) lines = g_strsplit(data ? data : "", "\n", -1);

    rc->resources = g_ptr_array_new_with_free_func((GDestroyNotify)resource_free);
    for (char **l = lines; *l; l++) {
        char *line = g_strstrip(*l);
        char *colon;
        Resource *r;

        if (!*line || *line == '!' || *line == '#')
            continue;
        colon = strchr(line, ':');
        if (!colon)
            continue;
        *colon = '\0';

        r = g_new0(Resource, 1);
        r->components = split_pattern(line);
        r->value = g_strstrip(g_strdup(colon + 1));
        g_ptr_array_add(rc->resources, r);
    }
    return rc;
}

LxpThemerc *lxp_themerc_load(const char *path, GError **error)
{
    g_autofree char *data = NULL;
    LxpThemerc *rc;

    if (!g_file_get_contents(path, &data, NULL, error))
        return NULL;
    rc = lxp_themerc_new_from_data(data);
    rc->dir = g_path_get_dirname(path);
    return rc;
}

LxpThemerc *lxp_themerc_load_theme(const char *name, GError **error)
{
    g_autofree char *dir = lxp_find_installed("themes", name, "openbox-3/themerc");
    g_autofree char *path = NULL;

    if (!dir) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_NOT_FOUND,
                    "Openbox theme “%s” is not installed", name);
        return NULL;
    }
    path = g_build_filename(dir, "openbox-3", "themerc", NULL);
    return lxp_themerc_load(path, error);
}

void lxp_themerc_free(LxpThemerc *rc)
{
    if (!rc)
        return;
    g_ptr_array_unref(rc->resources);
    g_free(rc->dir);
    g_free(rc);
}

const char *lxp_themerc_get_dir(const LxpThemerc *rc)
{
    return rc->dir;
}

/* Is score <a> more specific than <b>? Compared from the left. */
static gboolean more_specific(const int *a, const int *b, gsize n)
{
    for (gsize i = 0; i < n; i++)
        if (a[i] != b[i])
            return a[i] > b[i];
    return FALSE;
}

/*
 * Match pattern components <pat> against key components <key>. <score>
 * records how each key component was matched; the most specific complete
 * match is kept in <best>.
 */
static void match(char **pat, char **key, int *score, int *best,
                  gboolean *found, gsize n, gsize depth)
{
    if (!*pat) {
        if (!*key && (!*found || more_specific(score, best, n))) {
            memcpy(best, score, n * sizeof(int));
            *found = TRUE;
        }
        return;
    }
    if (g_str_equal(*pat, "*")) {
        match(pat + 1, key, score, best, found, n, depth);  /* zero components */
        if (*key) {                                          /* or one more */
            score[depth] = MATCH_SKIP;
            match(pat, key + 1, score, best, found, n, depth + 1);
        }
        return;
    }
    if (!*key)
        return;
    if (g_str_equal(*pat, "?") || g_str_equal(*pat, *key)) {
        score[depth] = g_str_equal(*pat, "?") ? MATCH_ANY : MATCH_NAME;
        match(pat + 1, key + 1, score, best, found, n, depth + 1);
    }
}

const char *lxp_themerc_lookup(const LxpThemerc *rc, const char *key)
{
    g_auto(GStrv) parts = split_pattern(key);
    gsize n = g_strv_length(parts);
    g_autofree int *score = g_new0(int, n + 1);
    g_autofree int *best = g_new0(int, n + 1);
    g_autofree int *winner = g_new0(int, n + 1);
    const char *value = NULL;

    for (guint i = 0; i < rc->resources->len; i++) {
        const Resource *r = g_ptr_array_index(rc->resources, i);
        gboolean found = FALSE;

        match(r->components, parts, score, best, &found, n, 0);
        /* Later definitions win ties, like a resource database merge. */
        if (found && (!value || !more_specific(winner, best, n))) {
            memcpy(winner, best, n * sizeof(int));
            value = r->value;
        }
    }
    return value;
}

gboolean lxp_themerc_color(const LxpThemerc *rc, LxpColor *color,
                           const char *key, ...)
{
    va_list args;
    gboolean ok = FALSE;

    va_start(args, key);
    for (const char *k = key; k && !ok; k = va_arg(args, const char *))
        ok = rc && lxp_color_parse(lxp_themerc_lookup(rc, k), color);
    va_end(args);
    return ok;
}

int lxp_themerc_int(const LxpThemerc *rc, const char *key, int fallback)
{
    const char *value = rc ? lxp_themerc_lookup(rc, key) : NULL;
    char *end;
    long v;

    if (!value)
        return fallback;
    v = strtol(value, &end, 10);
    return end != value ? (int)v : fallback;
}
