/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-libconfig.h"

#include "lxp-fileutil.h"

#include <glib/gstdio.h>
#include <string.h>

struct _LxpLibconfig {
    GString *text;
};

typedef struct {
    gsize value_start;
    gsize value_end;   /* exclusive, before any terminator */
} Span;

LxpLibconfig *lxp_libconfig_new_from_data(const char *data)
{
    LxpLibconfig *conf = g_new0(LxpLibconfig, 1);
    conf->text = g_string_new(data ? data : "");
    return conf;
}

LxpLibconfig *lxp_libconfig_load(const char *path, gboolean missing_ok, GError **error)
{
    g_autofree char *data = NULL;
    GError *local = NULL;

    if (!g_file_get_contents(path, &data, NULL, &local)) {
        if (missing_ok && g_error_matches(local, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_error_free(local);
            return lxp_libconfig_new_from_data("");
        }
        g_propagate_error(error, local);
        return NULL;
    }
    return lxp_libconfig_new_from_data(data);
}

gboolean lxp_libconfig_save(const LxpLibconfig *conf, const char *path, GError **error)
{
    GStatBuf st;
    int mode = 0644;

    if (g_stat(path, &st) == 0)
        mode = st.st_mode & 07777;
    return lxp_write_atomic(path, conf->text->str, (gssize)conf->text->len, mode, error);
}

void lxp_libconfig_free(LxpLibconfig *conf)
{
    if (!conf)
        return;
    g_string_free(conf->text, TRUE);
    g_free(conf);
}

const char *lxp_libconfig_to_data(const LxpLibconfig *conf)
{
    return conf->text->str;
}

static gboolean is_name_char(char c)
{
    return g_ascii_isalnum(c) || c == '-' || c == '_' || c == '*';
}

/* Skip a comment or string starting at <i>; returns the index after it. */
static gsize skip_comment_or_string(const char *s, gsize i, gsize len)
{
    if (s[i] == '#' || (s[i] == '/' && i + 1 < len && s[i + 1] == '/')) {
        while (i < len && s[i] != '\n')
            i++;
        return i;
    }
    if (s[i] == '/' && i + 1 < len && s[i + 1] == '*') {
        const char *end = strstr(s + i + 2, "*/");
        return end ? (gsize)(end - s) + 2 : len;
    }
    if (s[i] == '"') {
        for (i++; i < len && s[i] != '"'; i++)
            if (s[i] == '\\' && i + 1 < len)
                i++;
        return i < len ? i + 1 : len;
    }
    return i;
}

/* Locate top-level setting <name>. */
static gboolean find_setting(const char *s, gsize len, const char *name, Span *span)
{
    gsize name_len = strlen(name);
    int depth = 0;
    gboolean at_statement = TRUE;   /* next name starts a setting */

    for (gsize i = 0; i < len;) {
        gsize skipped = skip_comment_or_string(s, i, len);

        if (skipped != i) {
            i = skipped;
            continue;
        }
        if (s[i] == '{' || s[i] == '[' || s[i] == '(') {
            depth++;
            i++;
            continue;
        }
        if (s[i] == '}' || s[i] == ']' || s[i] == ')') {
            depth = MAX(0, depth - 1);
            i++;
            continue;
        }
        if (depth == 0 && (s[i] == ';' || s[i] == ',' || s[i] == '\n')) {
            at_statement = TRUE;
            i++;
            continue;
        }
        if (depth == 0 && at_statement && is_name_char(s[i])) {
            gsize start = i, j;

            while (i < len && is_name_char(s[i]))
                i++;
            j = i;
            while (j < len && (s[j] == ' ' || s[j] == '\t'))
                j++;
            at_statement = FALSE;
            if (j < len && (s[j] == '=' || s[j] == ':') && i - start == name_len &&
                strncmp(s + start, name, name_len) == 0) {
                gsize v = j + 1, end;

                while (v < len && (s[v] == ' ' || s[v] == '\t'))
                    v++;
                /* scalar value: up to the terminator, comment or end of line */
                end = v;
                while (end < len && s[end] != ';' && s[end] != ',' && s[end] != '\n' &&
                       s[end] != '#' && !(s[end] == '/' && end + 1 < len &&
                                          (s[end + 1] == '/' || s[end + 1] == '*'))) {
                    if (s[end] == '"')
                        end = skip_comment_or_string(s, end, len);
                    else
                        end++;
                }
                while (end > v && (s[end - 1] == ' ' || s[end - 1] == '\t'))
                    end--;
                span->value_start = v;
                span->value_end = end;
                return TRUE;
            }
            continue;
        }
        if (!g_ascii_isspace(s[i]))
            at_statement = FALSE;
        i++;
    }
    return FALSE;
}

char *lxp_libconfig_get(const LxpLibconfig *conf, const char *name)
{
    Span span;

    if (!find_setting(conf->text->str, conf->text->len, name, &span))
        return NULL;
    return g_strndup(conf->text->str + span.value_start, span.value_end - span.value_start);
}

void lxp_libconfig_set(LxpLibconfig *conf, const char *name, const char *value)
{
    Span span;
    g_autofree char *clean = g_strdup(value);

    /* one scalar on one line: nothing that could start another statement */
    g_strdelimit(clean, "\n\r;#{}[]()", ' ');
    if (find_setting(conf->text->str, conf->text->len, name, &span)) {
        g_string_erase(conf->text, (gssize)span.value_start,
                       (gssize)(span.value_end - span.value_start));
        g_string_insert(conf->text, (gssize)span.value_start, clean);
        return;
    }
    if (conf->text->len > 0 && conf->text->str[conf->text->len - 1] != '\n')
        g_string_append_c(conf->text, '\n');
    g_string_append_printf(conf->text, "%s = %s;\n", name, clean);
}
