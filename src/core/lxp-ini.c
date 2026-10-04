/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-ini.h"

#include "lxp-fileutil.h"

#include <glib/gstdio.h>
#include <string.h>

typedef enum {
    LINE_OTHER,    /* blank, comment, or unparsable: kept verbatim */
    LINE_GROUP,
    LINE_KEY,
} LineKind;

typedef struct {
    LineKind kind;
    char *text;    /* full line, without newline */
    char *group;   /* group this line belongs to (or names, for LINE_GROUP) */
    char *key;     /* LINE_KEY only */
    char *value;   /* LINE_KEY only, trimmed */
    char *sep;     /* LINE_KEY only: "=", " = "... */
} Line;

struct _LxpIni {
    GPtrArray *lines;
    gboolean trailing_newline;
};

static void line_free(Line *line)
{
    g_free(line->text);
    g_free(line->group);
    g_free(line->key);
    g_free(line->value);
    g_free(line->sep);
    g_free(line);
}

static Line *parse_line(const char *text, const char *group)
{
    Line *line = g_new0(Line, 1);
    const char *p = text;
    const char *eq;

    line->text = g_strdup(text);
    line->group = g_strdup(group);

    while (g_ascii_isspace(*p))
        p++;

    if (*p == '\0' || *p == '#' || *p == ';')
        return line;

    if (*p == '[') {
        const char *end = strrchr(p, ']');
        if (end) {
            line->kind = LINE_GROUP;
            g_free(line->group);
            line->group = g_strndup(p + 1, end - p - 1);
        }
        return line;
    }

    eq = strchr(p, '=');
    if (eq && eq > p) {
        const char *key_end = eq;
        const char *value = eq + 1;

        while (key_end > p && g_ascii_isspace(key_end[-1]))
            key_end--;
        while (*value == ' ' || *value == '\t')
            value++;

        line->kind = LINE_KEY;
        line->key = g_strndup(p, key_end - p);
        line->sep = g_strndup(key_end, value - key_end);
        line->value = g_strstrip(g_strdup(value));
    }
    return line;
}

LxpIni *lxp_ini_new(void)
{
    LxpIni *ini = g_new0(LxpIni, 1);

    ini->lines = g_ptr_array_new_with_free_func((GDestroyNotify)line_free);
    ini->trailing_newline = TRUE;
    return ini;
}

LxpIni *lxp_ini_new_from_data(const char *data)
{
    LxpIni *ini = lxp_ini_new();
    g_auto(GStrv) texts = NULL;
    const char *group = NULL;
    guint n;

    if (!data || !*data)
        return ini;

    texts = g_strsplit(data, "\n", -1);
    n = g_strv_length(texts);
    /* "a\nb\n" splits into {"a", "b", ""} */
    ini->trailing_newline = n > 0 && texts[n - 1][0] == '\0';
    if (ini->trailing_newline)
        n--;

    for (guint i = 0; i < n; i++) {
        char *text = texts[i];
        size_t len = strlen(text);
        Line *line;

        if (len > 0 && text[len - 1] == '\r')
            text[len - 1] = '\0';
        line = parse_line(text, group);
        if (line->kind == LINE_GROUP)
            group = line->group;
        g_ptr_array_add(ini->lines, line);
    }
    return ini;
}

LxpIni *lxp_ini_load(const char *path, gboolean missing_ok, GError **error)
{
    g_autofree char *data = NULL;
    GError *local = NULL;

    if (!g_file_get_contents(path, &data, NULL, &local)) {
        if (missing_ok && g_error_matches(local, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_error_free(local);
            return lxp_ini_new();
        }
        g_propagate_error(error, local);
        return NULL;
    }
    return lxp_ini_new_from_data(data);
}

char *lxp_ini_to_data(const LxpIni *ini)
{
    GString *out = g_string_new(NULL);

    for (guint i = 0; i < ini->lines->len; i++) {
        const Line *line = g_ptr_array_index(ini->lines, i);

        g_string_append(out, line->text);
        if (i + 1 < ini->lines->len || ini->trailing_newline)
            g_string_append_c(out, '\n');
    }
    return g_string_free(out, FALSE);
}

gboolean lxp_ini_save(const LxpIni *ini, const char *path, GError **error)
{
    g_autofree char *data = lxp_ini_to_data(ini);
    GStatBuf st;
    int mode = 0644;

    if (g_stat(path, &st) == 0)
        mode = st.st_mode & 07777;
    return lxp_write_atomic(path, data, -1, mode, error);
}

void lxp_ini_free(LxpIni *ini)
{
    if (!ini)
        return;
    g_ptr_array_unref(ini->lines);
    g_free(ini);
}

static Line *find_key(const LxpIni *ini, const char *group, const char *key,
                      guint *index)
{
    for (guint i = 0; i < ini->lines->len; i++) {
        Line *line = g_ptr_array_index(ini->lines, i);

        if (line->kind == LINE_KEY && g_strcmp0(line->group, group) == 0 &&
            g_str_equal(line->key, key)) {
            if (index)
                *index = i;
            return line;
        }
    }
    return NULL;
}

const char *lxp_ini_get(const LxpIni *ini, const char *group, const char *key)
{
    Line *line = find_key(ini, group, key, NULL);
    return line ? line->value : NULL;
}

/* Separator used by existing keys, so "a = b" files stay that way. */
static const char *preferred_sep(const LxpIni *ini)
{
    for (guint i = 0; i < ini->lines->len; i++) {
        const Line *line = g_ptr_array_index(ini->lines, i);
        if (line->kind == LINE_KEY)
            return line->sep;
    }
    return "=";
}

static gboolean is_blank(const Line *line)
{
    for (const char *p = line->text; *p; p++)
        if (!g_ascii_isspace(*p))
            return FALSE;
    return TRUE;
}

static void set_line_value(Line *line, const char *value)
{
    g_free(line->value);
    g_free(line->text);
    line->value = g_strdup(value);
    line->text = g_strconcat(line->key, line->sep, value, NULL);
}

void lxp_ini_set(LxpIni *ini, const char *group, const char *key,
                 const char *value)
{
    Line *line = find_key(ini, group, key, NULL);
    gint insert_at = -1;

    if (line) {
        if (g_strcmp0(line->value, value) != 0)
            set_line_value(line, value);
        return;
    }

    /* Insert after the last non-blank line of the group. */
    for (guint i = 0; i < ini->lines->len; i++) {
        Line *l = g_ptr_array_index(ini->lines, i);

        if (g_strcmp0(l->group, group) == 0 &&
            (l->kind == LINE_GROUP || !is_blank(l)))
            insert_at = (gint)i + 1;
    }
    if (insert_at < 0 && group == NULL)
        insert_at = 0;

    line = g_new0(Line, 1);
    line->kind = LINE_KEY;
    line->group = g_strdup(group);
    line->key = g_strdup(key);
    line->sep = g_strdup(preferred_sep(ini));
    set_line_value(line, value);

    if (insert_at >= 0) {
        g_ptr_array_insert(ini->lines, insert_at, line);
        return;
    }

    /* New group at the end of the file. */
    if (ini->lines->len > 0 &&
        !is_blank(g_ptr_array_index(ini->lines, ini->lines->len - 1)))
        g_ptr_array_add(ini->lines, parse_line("", NULL));
    {
        g_autofree char *header = g_strdup_printf("[%s]", group);
        g_ptr_array_add(ini->lines, parse_line(header, NULL));
    }
    g_ptr_array_add(ini->lines, line);
}

void lxp_ini_remove(LxpIni *ini, const char *group, const char *key)
{
    guint index;

    while (find_key(ini, group, key, &index))
        g_ptr_array_remove_index(ini->lines, index);
}

gboolean lxp_ini_has_group(const LxpIni *ini, const char *group)
{
    for (guint i = 0; i < ini->lines->len; i++) {
        const Line *line = g_ptr_array_index(ini->lines, i);
        if (g_strcmp0(line->group, group) == 0 &&
            (line->kind == LINE_GROUP || line->kind == LINE_KEY))
            return TRUE;
    }
    return FALSE;
}

void lxp_ini_remove_group(LxpIni *ini, const char *group)
{
    for (guint i = ini->lines->len; i > 0; i--) {
        const Line *line = g_ptr_array_index(ini->lines, i - 1);
        if (g_strcmp0(line->group, group) == 0)
            g_ptr_array_remove_index(ini->lines, i - 1);
    }
}

GStrv lxp_ini_get_groups(const LxpIni *ini)
{
    g_autoptr(GStrvBuilder) builder = g_strv_builder_new();

    for (guint i = 0; i < ini->lines->len; i++) {
        const Line *line = g_ptr_array_index(ini->lines, i);
        if (line->kind == LINE_GROUP)
            g_strv_builder_add(builder, line->group);
    }
    return g_strv_builder_end(builder);
}

GStrv lxp_ini_get_keys(const LxpIni *ini, const char *group)
{
    g_autoptr(GStrvBuilder) builder = g_strv_builder_new();

    for (guint i = 0; i < ini->lines->len; i++) {
        const Line *line = g_ptr_array_index(ini->lines, i);
        if (line->kind == LINE_KEY && g_strcmp0(line->group, group) == 0)
            g_strv_builder_add(builder, line->key);
    }
    return g_strv_builder_end(builder);
}

/* Mirrors QSettingsPrivate::iniEscapedString() for plain strings. */
char *lxp_qt_quote(const char *value)
{
    size_t len = strlen(value);
    gboolean quote = len > 0 && (g_ascii_isspace(value[0]) ||
                                 g_ascii_isspace(value[len - 1]));
    GString *out = g_string_sized_new(len + 2);

    if (value[0] == '@')
        g_string_append_c(out, '@');
    for (const char *p = value; *p; p++) {
        switch (*p) {
        case ';': case ',': case '=':
            quote = TRUE;
            g_string_append_c(out, *p);
            break;
        case '"':  g_string_append(out, "\\\""); break;
        case '\\': g_string_append(out, "\\\\"); break;
        case '\n': g_string_append(out, "\\n"); break;
        case '\r': g_string_append(out, "\\r"); break;
        case '\t': g_string_append(out, "\\t"); break;
        default:   g_string_append_c(out, *p); break;
        }
    }
    if (quote) {
        g_string_prepend_c(out, '"');
        g_string_append_c(out, '"');
    }
    return g_string_free(out, FALSE);
}

char *lxp_qt_unquote(const char *raw)
{
    GString *out = g_string_sized_new(strlen(raw));
    const char *p = raw;

    if (p[0] == '@' && p[1] == '@')
        p++;
    for (; *p; p++) {
        if (*p == '"')
            continue;
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n': g_string_append_c(out, '\n'); break;
            case 'r': g_string_append_c(out, '\r'); break;
            case 't': g_string_append_c(out, '\t'); break;
            default:  g_string_append_c(out, *p); break;
            }
            continue;
        }
        g_string_append_c(out, *p);
    }
    return g_string_free(out, FALSE);
}
