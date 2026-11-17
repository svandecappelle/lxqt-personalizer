/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-qss.h"

#include "lxp-image.h"

#include <math.h>
#include <string.h>

/* "45%", "0.45" or "115" (of 255) */
static double parse_alpha(const char *text)
{
    char *end;
    double v = g_ascii_strtod(text, &end);

    while (g_ascii_isspace(*end))
        end++;
    if (*end == '%')
        return CLAMP(v / 100, 0, 1);
    if (strchr(text, '.') || v <= 1)
        return CLAMP(v, 0, 1);
    return CLAMP(v / 255, 0, 1);
}

static gboolean palette_role(const LxpPreviewModel *p, const char *role, LxpColor *out)
{
    const struct {
        const char *name;
        const LxpColor *color;
    } roles[] = {
        { "window", &p->window }, { "window-text", &p->window_text },
        { "base", &p->base }, { "alternate-base", &p->alt_base }, { "text", &p->text },
        { "button", &p->button }, { "button-text", &p->button_text },
        { "highlight", &p->highlight }, { "highlighted-text", &p->highlighted_text },
        { "link", &p->link },
    };

    for (gsize i = 0; i < G_N_ELEMENTS(roles); i++)
        if (g_ascii_strcasecmp(roles[i].name, role) == 0) {
            *out = *roles[i].color;
            return TRUE;
        }
    return FALSE;
}

/*
 * The color starting at <*p>, which is moved past it. FALSE (and <*p> moved
 * past one word) when there is none here.
 */
static gboolean color_at(const char **p, const LxpPreviewModel *palette, LxpColor *out)
{
    const char *s = *p;
    const char *end = s;
    g_autofree char *word = NULL;

    while (*end && (g_ascii_isalnum(*end) || *end == '#' || *end == '-' || *end == '_'))
        end++;
    if (end == s) {
        *p = s + 1;
        return FALSE;
    }
    word = g_ascii_strdown(s, end - s);

    if (*end == '(') {
        const char *close = strchr(end, ')');
        g_autofree char *args = close ? g_strndup(end + 1, close - end - 1) : NULL;

        gboolean known = g_str_equal(word, "rgb") || g_str_equal(word, "rgba") ||
                         g_str_equal(word, "palette") || g_str_equal(word, "url");

        /* a gradient is read word by word: its stops are colors */
        if (!close || !known) {
            *p = end + 1;
            return FALSE;
        }
        *p = close + 1;
        if (g_str_equal(word, "rgb") || g_str_equal(word, "rgba")) {
            g_auto(GStrv) parts = g_strsplit(args, ",", -1);
            guint n = g_strv_length(parts);

            if (n < 3)
                return FALSE;
            out->r = CLAMP(g_ascii_strtod(parts[0], NULL) / 255, 0, 1);
            out->g = CLAMP(g_ascii_strtod(parts[1], NULL) / 255, 0, 1);
            out->b = CLAMP(g_ascii_strtod(parts[2], NULL) / 255, 0, 1);
            out->a = n > 3 ? parse_alpha(g_strstrip(parts[3])) : 1;
            return TRUE;
        }
        if (g_str_equal(word, "palette"))
            return palette_role(palette, g_strstrip(args), out);
        return FALSE;  /* url(), … */
    }

    *p = end;
    if (word[0] != '#' && !g_ascii_isalpha(word[0]))
        return FALSE;  /* "1px" */
    if (g_str_equal(word, "transparent")) {
        *out = (LxpColor){ 0, 0, 0, 0 };
        return TRUE;
    }
    return lxp_color_parse(word, out);
}

#define SAMPLE_SIZE 32

/* Color of <image> at (<fx>, <fy>), fractions of its size. */
static gboolean image_color(const char *image, double fx, double fy, LxpColor *out)
{
    cairo_surface_t *surface = lxp_image_load(image, SAMPLE_SIZE, SAMPLE_SIZE);
    const guchar *row;
    guint32 px;
    double a;

    if (!surface || cairo_image_surface_get_format(surface) != CAIRO_FORMAT_ARGB32)
        return FALSE;
    cairo_surface_flush(surface);
    row = cairo_image_surface_get_data(surface) +
          (int)(fy * (SAMPLE_SIZE - 1)) * cairo_image_surface_get_stride(surface);
    memcpy(&px, row + 4 * (int)(fx * (SAMPLE_SIZE - 1)), sizeof px);
    a = (px >> 24) / 255.0;
    *out = (LxpColor){ 0, 0, 0, a };
    if (a > 0) {  /* stored premultiplied */
        out->r = MIN(1, ((px >> 16) & 0xff) / 255.0 / a);
        out->g = MIN(1, ((px >> 8) & 0xff) / 255.0 / a);
        out->b = MIN(1, (px & 0xff) / 255.0 / a);
    }
    return TRUE;
}

/* The file of the first url() in <value>, inside <dir>; NULL without one. */
static char *value_image(const char *value, const char *dir)
{
    const char *url = strstr(value, "url(");
    const char *close = url ? strchr(url, ')') : NULL;
    g_autofree char *name = NULL;
    char *path;

    if (!dir || !close)
        return NULL;
    name = g_strndup(url + 4, close - url - 4);
    g_strstrip(name);
    if (*name == '"' || *name == '\'') {
        name[strlen(name) - 1] = '\0';
        memmove(name, name + 1, strlen(name));
    }
    /* themes only use their own files */
    if (!*name || g_path_is_absolute(name) || strstr(name, ".."))
        return NULL;
    path = g_build_filename(dir, name, NULL);
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR))
        g_clear_pointer(&path, g_free);
    return path;
}

static double color_distance(const LxpColor *a, const LxpColor *b)
{
    return fabs(a->r * a->a - b->r * b->a) + fabs(a->g * a->a - b->g * b->a) +
           fabs(a->b * a->a - b->b * b->a) + fabs(a->a - b->a);
}

/*
 * A button drawn with an image: its fill is the middle of the image, and a
 * side (or the outline) that stands out from it is its border.
 */
static void button_image(LxpPanelTaskStyle *b, const char *image)
{
    static const struct {
        char side;
        double fx, fy;
    } sides[] = { { 't', 0.5, 0.04 }, { 'l', 0.04, 0.5 }, { 'r', 0.96, 0.5 }, { 'b', 0.5, 0.96 } };
    LxpColor middle, edge[4];
    int standing = 0, last = -1;

    if (!image_color(image, 0.5, 0.5, &middle))
        return;
    b->bg = middle;
    b->has_bg = middle.a > 0.03;
    b->has_edge = FALSE;
    for (int i = 0; i < 4; i++) {
        double best = 0.3;
        gboolean stands = FALSE;

        /* the line may sit a little inside the picture's own outline */
        for (int step = 0; step < 5; step++) {
            double t = step * 0.06;
            double fx = sides[i].fx + (sides[i].fx < 0.5 ? t : sides[i].fx > 0.5 ? -t : 0);
            double fy = sides[i].fy + (sides[i].fy < 0.5 ? t : sides[i].fy > 0.5 ? -t : 0);
            LxpColor c;

            if (image_color(image, fx, fy, &c) && c.a > 0.3 &&
                color_distance(&c, &middle) > best) {
                best = color_distance(&c, &middle);
                edge[i] = c;
                stands = TRUE;
            }
        }
        if (stands) {
            standing++;
            last = i;
        }
    }
    if (standing == 0)
        return;
    b->has_edge = TRUE;
    b->edge = edge[last];
    b->edge.a = 1;
    b->edge_side = standing >= 3 ? 'a' : sides[last].side;
}

/* First and last color of <value>; FALSE without any. */
static gboolean value_colors(const char *value, const LxpPreviewModel *palette,
                             LxpColor *first, LxpColor *last)
{
    const char *p = value;
    gboolean found = FALSE;
    LxpColor c;

    while (*p) {
        if (!color_at(&p, palette, &c))
            continue;
        if (!found)
            *first = c;
        *last = c;
        found = TRUE;
    }
    return found;
}

/* "2px solid #e95420" -> width 2 and the color; FALSE for no visible border. */
static gboolean border_value(const char *value, const LxpPreviewModel *palette, LxpColor *color)
{
    LxpColor last;
    char *end;
    double width = g_ascii_strtod(value, &end);

    if (strstr(value, "none") || (end != value && width < 1))
        return FALSE;
    return value_colors(value, palette, color, &last) && color->a > 0;
}

static void button_property(LxpPanelTaskStyle *b, const char *name, const char *value,
                            const LxpPreviewModel *palette, const char *dir)
{
    g_autofree char *image = NULL;
    LxpColor first, last;

    if ((g_str_equal(name, "border-image") || g_str_has_prefix(name, "background")) &&
        (image = value_image(value, dir))) {
        button_image(b, image);
        return;
    }

    if (g_str_equal(name, "background") || g_str_equal(name, "background-color")) {
        if (value_colors(value, palette, &first, &last)) {
            b->bg = lxp_color_mix(&first, &last, 0.5);
            b->bg.a = (first.a + last.a) / 2;
            b->has_bg = b->bg.a > 0;
        }
    } else if (g_str_equal(name, "color")) {
        b->has_text = value_colors(value, palette, &b->text, &last);
    } else if (g_str_has_prefix(name, "border") && !strstr(name, "radius") &&
               !strstr(name, "image") && !strstr(name, "width") && !strstr(name, "style")) {
        const char *side = name + strlen("border");
        char where = g_str_has_prefix(side, "-top") ? 't' : g_str_has_prefix(side, "-bottom") ? 'b'
                   : g_str_has_prefix(side, "-left") ? 'l' : g_str_has_prefix(side, "-right") ? 'r'
                   : 'a';

        /* "border-bottom-color: …" recolors the side an earlier rule drew */
        if (g_str_has_suffix(name, "-color")) {
            if (b->has_edge && value_colors(value, palette, &first, &last))
                b->edge = first;
        } else if (border_value(value, palette, &first)) {
            b->has_edge = TRUE;
            b->edge = first;
            b->edge_side = where;
        } else if (where == 'a' || where == b->edge_side) {
            b->has_edge = FALSE;
        }
    }
}

typedef enum {
    RULE_NONE, RULE_PANEL, RULE_BACKGROUND, RULE_BUTTON, RULE_TASK, RULE_TASK_ON, RULE_MENU,
} RuleKind;

/* What <selector> styles, for a panel at <position>. */
static RuleKind selector_kind(const char *selector, const char *position)
{
    g_autoptr(GString) plain = g_string_new(NULL);
    g_auto(GStrv) words = NULL;
    guint n;

    /* drop [attribute] tests: only the panel position may be asked for */
    for (const char *p = selector; *p; p++) {
        if (*p == '[') {
            const char *close = strchr(p, ']');
            g_autofree char *test = close ? g_strndup(p + 1, close - p - 1) : NULL;
            g_autofree char *want = g_strdup_printf("position=\"%s\"", position);

            if (!test || !g_str_equal(test, want))
                return RULE_NONE;
            p = close;
        } else {
            g_string_append_c(plain, *p);
        }
    }
    words = g_strsplit_set(g_strstrip(plain->str), " \t\r\n>", -1);
    n = 0;
    for (guint i = 0; words[i]; i++)
        if (*words[i])
            words[n++] = words[i];
        else
            g_free(words[i]);
    words[n] = NULL;

    if (n == 1 && g_str_equal(words[0], "LXQtPanel"))
        return RULE_PANEL;
    if (n > 1 && g_str_equal(words[0], "LXQtPanel")) {
        /* "LXQtPanel X": the same as "X" for what is drawn here */
        g_free(words[0]);
        memmove(words, words + 1, n * sizeof *words);
        n--;
    }
    if (n == 0 || n > 3)
        return RULE_NONE;
    if (n == 1 && g_str_equal(words[0], "#BackgroundWidget"))
        return RULE_BACKGROUND;
    if (n == 1 && (g_str_equal(words[0], "#MainMenu") || g_str_equal(words[0], "#FancyMenu")))
        return RULE_MENU;
    /* every button of the panel's plugins, task buttons included */
    if (g_str_equal(words[n - 1], "QToolButton") &&
        (g_str_equal(words[n - 2 > 0 ? n - 2 : 0], "Plugin") ||
         g_str_equal(words[n - 2 > 0 ? n - 2 : 0], "LXQtPanelPlugin") ||
         (n == 3 && g_str_equal(words[1], "QWidget") &&
          (g_str_equal(words[0], "Plugin") || g_str_equal(words[0], "LXQtPanelPlugin")))))
        return RULE_BUTTON;
    if (n == 2 && g_str_equal(words[0], "#TaskBar")) {
        if (g_str_equal(words[n - 1], "QToolButton"))
            return RULE_TASK;
        if (g_str_equal(words[n - 1], "QToolButton:on") ||
            g_str_equal(words[n - 1], "QToolButton:checked"))
            return RULE_TASK_ON;
    }
    return RULE_NONE;
}

static void apply_declaration(LxpPanelQss *out, RuleKind kind, const char *name,
                              const char *value, const LxpPreviewModel *palette,
                              const char *dir)
{
    g_autofree char *image = NULL;
    LxpColor first, last;

    switch (kind) {
    case RULE_MENU:
        if (g_str_equal(name, "qproperty-icon") && (image = value_image(value, dir))) {
            g_free(out->menu_icon);
            out->menu_icon = g_steal_pointer(&image);
        }
        break;
    case RULE_BUTTON:
        if (g_str_equal(name, "color") && value_colors(value, palette, &first, &last)) {
            out->has_button_text = TRUE;
            out->button_text = first;
        }
        break;
    case RULE_PANEL:
    case RULE_BACKGROUND:
        if (kind == RULE_BACKGROUND &&
            (g_str_equal(name, "border-image") || g_str_has_prefix(name, "background")) &&
            (image = value_image(value, dir))) {
            /* a picture of the panel: shaded like it across its thickness */
            gboolean across = g_str_equal(out->position, "Left") ||
                              g_str_equal(out->position, "Right");

            /* a border image is stretched from its middle: its sides are lines */
            double from = g_str_equal(name, "border-image") ? 0.5 : 0.25;

            if (image_color(image, across ? from : 0.5, across ? 0.5 : from, &first) &&
                image_color(image, across ? 1 - from : 0.5, across ? 0.5 : 1 - from, &last)) {
                out->has_bg = TRUE;
                out->bg = first;
                out->bg_to = last;
            }
        } else if (g_str_equal(name, "color")) {
            out->has_text = value_colors(value, palette, &out->text, &last) || out->has_text;
        } else if ((g_str_equal(name, "background") || g_str_equal(name, "background-color")) &&
                   (kind == RULE_BACKGROUND || !out->has_bg) &&
                   value_colors(value, palette, &first, &last)) {
            out->has_bg = TRUE;
            out->bg = first;
            out->bg_to = last;
        }
        break;
    case RULE_TASK:
        button_property(&out->task, name, value, palette, dir);
        break;
    case RULE_TASK_ON:
        button_property(&out->task_active, name, value, palette, dir);
        break;
    case RULE_NONE:
        break;
    }
}

/* <qss> without its comments. */
static char *strip_comments(const char *qss)
{
    GString *out = g_string_new(NULL);

    for (const char *p = qss; *p; p++) {
        if (p[0] == '/' && p[1] == '*') {
            const char *close = strstr(p + 2, "*/");
            if (!close)
                break;
            p = close + 1;
        } else {
            g_string_append_c(out, *p);
        }
    }
    return g_string_free(out, FALSE);
}

void lxp_panel_qss_parse(const char *qss, const char *dir, const char *position,
                         const LxpPreviewModel *palette, LxpPanelQss *out)
{
    g_autofree char *text = strip_comments(qss);
    const char *p = text;

    memset(out, 0, sizeof *out);
    out->position = position;
    while (*p) {
        const char *open = strchr(p, '{');
        const char *close = open ? strchr(open, '}') : NULL;
        g_autofree char *selectors = NULL;
        g_autofree char *body = NULL;
        g_auto(GStrv) list = NULL;
        g_auto(GStrv) declarations = NULL;

        if (!close)
            break;
        selectors = g_strndup(p, open - p);
        body = g_strndup(open + 1, close - open - 1);
        p = close + 1;

        list = g_strsplit(selectors, ",", -1);
        declarations = g_strsplit(body, ";", -1);
        for (char **sel = list; *sel; sel++) {
            RuleKind kind = selector_kind(*sel, position);

            if (kind == RULE_NONE)
                continue;
            for (char **d = declarations; *d; d++) {
                char *colon = strchr(*d, ':');
                g_autofree char *name = NULL;
                g_autofree char *value = NULL;

                if (!colon)
                    continue;
                name = g_ascii_strdown(*d, colon - *d);
                value = g_strdup(colon + 1);
                apply_declaration(out, kind, g_strstrip(name), g_strstrip(value), palette, dir);
            }
        }
    }

    /* task buttons are plugin buttons */
    if (!out->task.has_text && out->has_button_text) {
        out->task.has_text = TRUE;
        out->task.text = out->button_text;
    }
    /* the pressed button keeps what the plain one sets and it does not change */
    if (!out->task_active.has_text && out->task.has_text) {
        out->task_active.has_text = TRUE;
        out->task_active.text = out->task.text;
    }
    if (!out->task_active.has_edge && out->task.has_edge) {
        out->task_active.has_edge = TRUE;
        out->task_active.edge = out->task.edge;
        out->task_active.edge_side = out->task.edge_side;
    }
    if (!out->task_active.has_bg && out->task.has_bg) {
        out->task_active.has_bg = TRUE;
        out->task_active.bg = out->task.bg;
    }
}

void lxp_panel_qss_clear(LxpPanelQss *qss)
{
    g_clear_pointer(&qss->menu_icon, g_free);
}

gboolean lxp_panel_qss_load(const char *path, const char *position,
                            const LxpPreviewModel *palette, LxpPanelQss *out)
{
    g_autofree char *qss = NULL;
    g_autofree char *dir = g_path_get_dirname(path);

    memset(out, 0, sizeof *out);
    if (!g_file_get_contents(path, &qss, NULL, NULL))
        return FALSE;
    lxp_panel_qss_parse(qss, dir, position, palette, out);
    return TRUE;
}
