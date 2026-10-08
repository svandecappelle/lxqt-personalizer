/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-kvantum-gen.h"

#include "lxp-fileutil.h"

#include <math.h>
#include <string.h>

#define STRETCH     8      /* size of the stretched middle cells */
#define ROW_WIDTH   1600   /* where the SVG layout wraps */
#define CHECK_SIZE  16
#define SCROLL_WIDTH 10
#define SLIDER_WIDTH 4

typedef struct {
    GString *body;
    double x, y, row_height;
} Svg;

/* How one state of a frame element looks. */
typedef struct {
    LxpColor fill;
    double   fill_alpha;
    LxpColor border;
    double   border_alpha;
} Look;

/* --- SVG output ----------------------------------------------------------- */

static void color_attr(GString *out, const char *what, const LxpColor *c, double alpha)
{
    g_autofree char *hex = lxp_color_to_hex(c);
    char a[G_ASCII_DTOSTR_BUF_SIZE];

    g_string_append_printf(out, " %s=\"%s\" %s-opacity=\"%s\"", what, hex, what,
                           g_ascii_formatd(a, sizeof(a), "%.3f", CLAMP(alpha, 0, 1)));
}

static void num(GString *out, double v)
{
    char buf[G_ASCII_DTOSTR_BUF_SIZE];
    g_string_append(out, g_ascii_formatd(buf, sizeof(buf), "%.2f", v));
}

static void rect(GString *out, double x, double y, double w, double h, const LxpColor *c,
                 double alpha)
{
    g_string_append(out, "<rect x=\"");
    num(out, x);
    g_string_append(out, "\" y=\"");
    num(out, y);
    g_string_append(out, "\" width=\"");
    num(out, w);
    g_string_append(out, "\" height=\"");
    num(out, h);
    g_string_append_c(out, '"');
    color_attr(out, "fill", c, alpha);
    g_string_append(out, "/>");
}

/* Reserve a <w> x <h> block in the layout; returns its origin. */
static void place(Svg *svg, double w, double h, double *x, double *y)
{
    if (svg->x + w > ROW_WIDTH) {
        svg->x = 0;
        svg->y += svg->row_height + 4;
        svg->row_height = 0;
    }
    *x = svg->x;
    *y = svg->y;
    svg->x += w + 4;
    svg->row_height = MAX(svg->row_height, h);
}

/*
 * Corner piece of a rounded rectangle, drawn for the top-left corner and
 * mirrored by <flip_x>/<flip_y>. The fill covers the whole cell except the
 * rounded-off part, so the element's bounds are exactly the cell.
 */
static void corner(GString *out, double cx, double cy, int f, int r, double b,
                   gboolean flip_x, gboolean flip_y, const Look *look)
{
    double h = b / 2;
    int sweep = flip_x == flip_y ? 1 : 0;
#define PX(u) (cx + (flip_x ? f - (u) : (u)))
#define PY(v) (cy + (flip_y ? f - (v) : (v)))

    g_string_append(out, "<path d=\"M ");
    if (r > 0) {
        num(out, PX(0)); g_string_append_c(out, ','); num(out, PY(r));
        g_string_append_printf(out, " A %d,%d 0 0 %d ", r, r, sweep);
        num(out, PX(r)); g_string_append_c(out, ','); num(out, PY(0));
    } else {
        num(out, PX(0)); g_string_append_c(out, ','); num(out, PY(0));
    }
    g_string_append(out, " L ");
    num(out, PX(f)); g_string_append_c(out, ','); num(out, PY(0));
    g_string_append(out, " L ");
    num(out, PX(f)); g_string_append_c(out, ','); num(out, PY(f));
    g_string_append(out, " L ");
    num(out, PX(0)); g_string_append_c(out, ','); num(out, PY(f));
    g_string_append(out, " Z\"");
    color_attr(out, "fill", &look->fill, look->fill_alpha);
    g_string_append(out, "/>");

    if (b <= 0 || look->border_alpha <= 0)
        return;
    g_string_append(out, "<path fill=\"none\" d=\"M ");
    num(out, PX(h)); g_string_append_c(out, ','); num(out, PY(f));
    g_string_append(out, " L ");
    num(out, PX(h)); g_string_append_c(out, ','); num(out, PY(MAX(r, h)));
    if (r > h) {
        g_string_append(out, " A ");
        num(out, r - h); g_string_append_c(out, ','); num(out, r - h);
        g_string_append_printf(out, " 0 0 %d ", sweep);
        num(out, PX(r)); g_string_append_c(out, ','); num(out, PY(h));
    }
    g_string_append(out, " L ");
    num(out, PX(f)); g_string_append_c(out, ','); num(out, PY(h));
    g_string_append_c(out, '"');
    color_attr(out, "stroke", &look->border, look->border_alpha);
    g_string_append(out, " stroke-width=\"");
    num(out, b);
    g_string_append(out, "\"/>");
#undef PX
#undef PY
}

/*
 * One state of a frame element: interior "<element>-<state>" and the eight
 * frame pieces around it, cut from a rounded rectangle.
 */
static void frame_element(Svg *svg, const char *element, const char *state, int f, int r,
                          const Look *look)
{
    double b = look->border_alpha > 0 ? 1 : 0;
    double x, y, k = STRETCH, w = 2 * f + k;
    GString *o = svg->body;
    struct { const char *suffix; int col, row; } pieces[] = {
        { "-topleft", 0, 0 }, { "-top", 1, 0 }, { "-topright", 2, 0 },
        { "-left", 0, 1 }, { "", 1, 1 }, { "-right", 2, 1 },
        { "-bottomleft", 0, 2 }, { "-bottom", 1, 2 }, { "-bottomright", 2, 2 },
    };

    r = MIN(r, f);
    place(svg, w, w, &x, &y);
    for (gsize i = 0; i < G_N_ELEMENTS(pieces); i++) {
        int col = pieces[i].col, row = pieces[i].row;
        double px = x + (col == 0 ? 0 : col == 1 ? f : f + k);
        double py = y + (row == 0 ? 0 : row == 1 ? f : f + k);
        double pw = col == 1 ? k : f, ph = row == 1 ? k : f;

        g_string_append_printf(o, "<g id=\"%s-%s%s\">", element, state, pieces[i].suffix);
        if (col != 1 && row != 1) {
            corner(o, px, py, f, r, b, col == 2, row == 2, look);
        } else {
            rect(o, px, py, pw, ph, &look->fill, look->fill_alpha);
            if (b > 0) {
                if (row == 0 && col == 1)
                    rect(o, px, py, pw, b, &look->border, look->border_alpha);
                else if (row == 2 && col == 1)
                    rect(o, px, py + ph - b, pw, b, &look->border, look->border_alpha);
                else if (col == 0 && row == 1)
                    rect(o, px, py, b, ph, &look->border, look->border_alpha);
                else if (col == 2 && row == 1)
                    rect(o, px + pw - b, py, b, ph, &look->border, look->border_alpha);
            }
        }
        g_string_append(o, "</g>\n");
    }
}

/* An indicator: a <size> x <size> element with explicit bounds. */
static void indicator_begin(Svg *svg, const char *id, int size, double *x, double *y)
{
    LxpColor none = { 0, 0, 0, 1 };

    place(svg, size, size, x, y);
    g_string_append_printf(svg->body, "<g id=\"%s\">", id);
    rect(svg->body, *x, *y, size, size, &none, 0);  /* bounds */
}

static void stroke_path(GString *o, const char *d, const LxpColor *c, double alpha, double width)
{
    g_string_append_printf(o, "<path fill=\"none\" stroke-linecap=\"round\" "
                              "stroke-linejoin=\"round\" d=\"%s\"", d);
    color_attr(o, "stroke", c, alpha);
    g_string_append(o, " stroke-width=\"");
    num(o, width);
    g_string_append(o, "\"/>");
}

static char *path_at(double x, double y, const char *fmt, const double *pts, int n)
{
    GString *d = g_string_new(NULL);
    int p = 0;

    for (const char *c = fmt; *c; c++) {
        if (*c == '%') {
            char buf[G_ASCII_DTOSTR_BUF_SIZE];
            g_string_append(d, g_ascii_formatd(buf, sizeof(buf), "%.2f",
                                               pts[p] + (p % 2 ? y : x)));
            p++;
            if (p > n)
                break;
        } else {
            g_string_append_c(d, *c);
        }
    }
    return g_string_free(d, FALSE);
}

static void rounded_box(GString *o, double x, double y, double w, double h, double r,
                        const LxpColor *fill, double fill_alpha, const LxpColor *border,
                        double border_alpha)
{
    g_string_append(o, "<rect x=\"");
    num(o, x + 0.5);
    g_string_append(o, "\" y=\"");
    num(o, y + 0.5);
    g_string_append(o, "\" width=\"");
    num(o, w - 1);
    g_string_append(o, "\" height=\"");
    num(o, h - 1);
    g_string_append(o, "\" rx=\"");
    num(o, r);
    g_string_append(o, "\" ry=\"");
    num(o, r);
    g_string_append_c(o, '"');
    color_attr(o, "fill", fill, fill_alpha);
    if (border)
        color_attr(o, "stroke", border, border_alpha);
    g_string_append(o, " stroke-width=\"1\"/>");
}

/* --- the theme ----------------------------------------------------------- */

static Look look(const LxpColor *fill, double fill_alpha, const LxpColor *border,
                 double border_alpha)
{
    Look l = { *fill, fill_alpha, *border, border_alpha };
    return l;
}

static void element_states(Svg *svg, const char *element, int f, int r, const Look *normal,
                           const Look *focused, const Look *pressed, const Look *toggled,
                           const Look *disabled)
{
    frame_element(svg, element, "normal", f, r, normal);
    frame_element(svg, element, "focused", f, r, focused);
    frame_element(svg, element, "pressed", f, r, pressed);
    frame_element(svg, element, "toggled", f, r, toggled);
    frame_element(svg, element, "disabled", f, r, disabled);
}

static void indicators(Svg *svg, const LxpSchemeColors *c)
{
    static const struct { const char *state; } states[] = {
        { "normal" }, { "focused" }, { "pressed" }, { "disabled" },
    };
    const char *dirs[] = { "up", "down", "left", "right", "plus", "minus" };
    /* chevrons in a 10x10 box */
    const char *chevrons[] = {
        "M %,% L %,% L %,%", "M %,% L %,% L %,%", "M %,% L %,% L %,%",
        "M %,% L %,% L %,%", "M %,% L %,% L %,%", "M %,% L %,% L %,%",
    };
    const double pts[][6] = {
        { 2, 6.5, 5, 3.5, 8, 6.5 }, { 2, 3.5, 5, 6.5, 8, 3.5 },
        { 6.5, 2, 3.5, 5, 6.5, 8 }, { 3.5, 2, 6.5, 5, 3.5, 8 },
        { 3.5, 2, 6.5, 5, 3.5, 8 }, { 2, 3.5, 5, 6.5, 8, 3.5 },
    };
    LxpColor box_border = lxp_color_mix(&c->fg, &c->surface, 0.55);
    LxpColor accent_light = lxp_color_mix(&c->accent, &c->fg, 0.15);
    double x, y;

    for (gsize s = 0; s < G_N_ELEMENTS(states); s++) {
        const char *st = states[s].state;
        gboolean hover = g_str_equal(st, "focused");
        gboolean off = g_str_equal(st, "disabled");
        double a = off ? 0.4 : 1;
        const LxpColor *on = hover ? &accent_light : &c->accent;
        g_autofree char *id = NULL;

        /* check box */
        id = g_strdup_printf("checkbox-%s", st);
        indicator_begin(svg, id, CHECK_SIZE, &x, &y);
        rounded_box(svg->body, x, y, CHECK_SIZE, CHECK_SIZE, 3.5, &c->surface, a,
                    hover ? &c->accent : &box_border, a);
        g_string_append(svg->body, "</g>\n");
        g_clear_pointer(&id, g_free);

        id = g_strdup_printf("checkbox-checked-%s", st);
        indicator_begin(svg, id, CHECK_SIZE, &x, &y);
        rounded_box(svg->body, x, y, CHECK_SIZE, CHECK_SIZE, 3.5, on, a, on, a);
        {
            const double p[] = { 4, 8.5, 7, 11.5, 12, 5 };
            g_autofree char *d = path_at(x, y, "M %,% L %,% L %,%", p, 6);
            stroke_path(svg->body, d, &c->accent_fg, a, 2);
        }
        g_string_append(svg->body, "</g>\n");
        g_clear_pointer(&id, g_free);

        id = g_strdup_printf("checkbox-tristate-%s", st);
        indicator_begin(svg, id, CHECK_SIZE, &x, &y);
        rounded_box(svg->body, x, y, CHECK_SIZE, CHECK_SIZE, 3.5, on, a, on, a);
        {
            const double p[] = { 4.5, 8, 11.5, 8 };
            g_autofree char *d = path_at(x, y, "M %,% L %,%", p, 4);
            stroke_path(svg->body, d, &c->accent_fg, a, 2);
        }
        g_string_append(svg->body, "</g>\n");
        g_clear_pointer(&id, g_free);

        /* radio button */
        id = g_strdup_printf("radio-%s", st);
        indicator_begin(svg, id, CHECK_SIZE, &x, &y);
        rounded_box(svg->body, x, y, CHECK_SIZE, CHECK_SIZE, CHECK_SIZE / 2.0, &c->surface, a,
                    hover ? &c->accent : &box_border, a);
        g_string_append(svg->body, "</g>\n");
        g_clear_pointer(&id, g_free);

        id = g_strdup_printf("radio-checked-%s", st);
        indicator_begin(svg, id, CHECK_SIZE, &x, &y);
        rounded_box(svg->body, x, y, CHECK_SIZE, CHECK_SIZE, CHECK_SIZE / 2.0, on, a, on, a);
        rounded_box(svg->body, x + 5, y + 5, 6, 6, 3, &c->accent_fg, a, NULL, 0);
        g_string_append(svg->body, "</g>\n");
        g_clear_pointer(&id, g_free);

        /* arrows, also used for spin boxes and tree expanders */
        for (gsize d = 0; d < G_N_ELEMENTS(dirs); d++) {
            g_autofree char *aid = g_strdup_printf("arrow-%s-%s", dirs[d], st);
            g_autofree char *path = NULL;
            const LxpColor *col = g_str_equal(st, "pressed") ? &c->accent : &c->fg;

            indicator_begin(svg, aid, 10, &x, &y);
            path = path_at(x, y, chevrons[d], pts[d], 6);
            stroke_path(svg->body, path, col, off ? 0.4 : 0.85, 1.5);
            g_string_append(svg->body, "</g>\n");
        }

        /* slider handle */
        {
            g_autofree char *sid = g_strdup_printf("slidercursor-%s", st);
            indicator_begin(svg, sid, CHECK_SIZE, &x, &y);
            rounded_box(svg->body, x, y, CHECK_SIZE, CHECK_SIZE, CHECK_SIZE / 2.0,
                        on, a, on, a);
            rounded_box(svg->body, x + 5, y + 5, 6, 6, 3, &c->accent_fg, a, NULL, 0);
            g_string_append(svg->body, "</g>\n");
        }
    }
}

static char *build_svg(const LxpSchemeColors *c, int radius)
{
    Svg svg = { g_string_new(NULL), 0, 0, 0 };
    LxpColor button = c->dark ? lxp_color_mix(&c->bg, &c->fg, 0.10) : c->surface;
    LxpColor button_hover = lxp_color_mix(&button, &c->fg, 0.07);
    LxpColor button_press = lxp_color_mix(&button, &c->fg, 0.14);
    LxpColor tint = lxp_color_mix(&button, &c->accent, c->dark ? 0.45 : 0.25);
    LxpColor hover_border = lxp_color_mix(&c->border, &c->accent, 0.6);
    LxpColor subtle = lxp_color_mix(&c->bg, &c->fg, 0.08);
    LxpColor menu_bg = c->dark ? lxp_color_mix(&c->bg, &c->fg, 0.05) : c->surface;
    LxpColor groove = lxp_color_mix(&c->bg, &c->fg, 0.15);
    int r = CLAMP(radius, 0, 16);
    int fb = MAX(r, 3);                         /* buttons, fields, tabs */
    int rm = MIN(r + 2, 12), fm = MAX(rm, 3);   /* menus */
    int ri = MAX(r - 2, 0), fi = MAX(ri, 2);    /* menu items, selections */
    int rs = SCROLL_WIDTH / 2;                  /* scrollbar pills */
    int rg = SLIDER_WIDTH / 2;                  /* slider grooves */
    Look n, f, p, t, d;

    /* push buttons and combo boxes */
    n = look(&button, 1, &c->border, 1);
    f = look(&button_hover, 1, &hover_border, 1);
    p = look(&button_press, 1, &c->border, 1);
    t = look(&tint, 1, &c->accent, 1);
    d = look(&button, 0.5, &c->border, 0.5);
    element_states(&svg, "button", fb, r, &n, &f, &p, &t, &d);

    /* tool buttons: flat until hovered */
    n = look(&button, 0, &c->border, 0);
    f = look(&button_hover, 1, &c->border, 0);
    p = look(&button_press, 1, &c->border, 0);
    t = look(&tint, 1, &c->border, 0);
    d = look(&button, 0, &c->border, 0);
    element_states(&svg, "tbutton", fb, r, &n, &f, &p, &t, &d);

    /* text fields */
    n = look(&c->surface, 1, &c->border, 1);
    f = look(&c->surface, 1, &c->accent, 1);
    p = f;
    t = f;
    d = look(&c->surface, 0.5, &c->border, 0.5);
    element_states(&svg, "lineedit", fb, r, &n, &f, &p, &t, &d);

    /* popup menus and tooltips */
    n = look(&menu_bg, 1, &c->border, 1);
    element_states(&svg, "menu", fm, rm, &n, &n, &n, &n, &n);
    n = look(&c->bg_alt, 1, &c->border, 1);
    element_states(&svg, "tooltip", fm, MIN(rm, 8), &n, &n, &n, &n, &n);

    /* menu items and list selections */
    n = look(&c->accent, 0, &c->accent, 0);
    f = look(&c->accent, 1, &c->accent, 0);
    element_states(&svg, "menuitem", fi, ri, &n, &f, &f, &f, &n);
    element_states(&svg, "menubaritem", fi, ri, &n, &f, &f, &f, &n);
    f = look(&c->accent, 0.18, &c->accent, 0);
    p = look(&c->accent, 1, &c->accent, 0);
    element_states(&svg, "itemview", fi, ri, &n, &f, &p, &p, &n);

    /* tabs and frames */
    n = look(&subtle, 1, &c->border, 0);
    f = look(&button_hover, 1, &c->border, 0);
    t = look(&c->bg, 1, &c->border, 1);
    element_states(&svg, "tab", fb, r, &n, &f, &t, &t, &n);
    n = look(&c->bg, 0, &c->border, 1);
    element_states(&svg, "tabframe", fb, r, &n, &n, &n, &n, &n);
    element_states(&svg, "common", fb, r, &n, &n, &n, &n, &n);

    /* scrollbars: thin pills */
    n = look(&c->fg, 0.30, &c->fg, 0);
    f = look(&c->fg, 0.45, &c->fg, 0);
    p = look(&c->fg, 0.60, &c->fg, 0);
    element_states(&svg, "scrollbarslider", rs, rs, &n, &f, &p, &p, &n);
    n = look(&c->fg, 0, &c->fg, 0);
    element_states(&svg, "scrollbargroove", rs, rs, &n, &n, &n, &n, &n);

    /* sliders and progress bars */
    n = look(&groove, 1, &groove, 0);
    t = look(&c->accent, 1, &c->accent, 0);
    element_states(&svg, "slider", rg, rg, &n, &n, &n, &t, &n);
    n = look(&groove, 1, &groove, 0);
    element_states(&svg, "progress", 3, MIN(r, 3), &n, &n, &n, &n, &n);
    t = look(&c->accent, 1, &c->accent, 0);
    element_states(&svg, "progress-pattern", 3, MIN(r, 3), &t, &t, &t, &t, &t);

    indicators(&svg, c);

    {
        GString *out = g_string_new(NULL);
        g_string_append_printf(out,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!-- generated by lxqt-personalizer, regenerate instead of editing -->\n"
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\">\n",
            ROW_WIDTH, (int)(svg.y + svg.row_height + 4));
        g_string_append(out, svg.body->str);
        g_string_append(out, "</svg>\n");
        g_string_free(svg.body, TRUE);
        return g_string_free(out, FALSE);
    }
}

static char *build_kvconfig(const LxpSchemeColors *c, const char *name, int radius)
{
    g_autofree char *window = lxp_color_to_hex(&c->bg);
    g_autofree char *base = lxp_color_to_hex(&c->surface);
    g_autofree char *alt = lxp_color_to_hex(&c->bg_alt);
    g_autofree char *text = lxp_color_to_hex(&c->fg);
    g_autofree char *dim = lxp_color_to_hex(&c->fg_dim);
    g_autofree char *accent = lxp_color_to_hex(&c->accent);
    g_autofree char *accent_fg = lxp_color_to_hex(&c->accent_fg);
    g_autofree char *link = lxp_color_to_hex(&c->link);
    g_autofree char *visited = lxp_color_to_hex(&c->link_visited);
    g_autofree char *border = lxp_color_to_hex(&c->border);
    int fb = MAX(CLAMP(radius, 0, 16), 3);
    int fm = MAX(MIN(radius + 2, 12), 3);
    int fi = MAX(radius - 2, 2);

    return g_strdup_printf(
        "# %s — generated by lxqt-personalizer, regenerate instead of editing.\n\n"
        "[%%General]\n"
        "author=lxqt-personalizer\n"
        "comment=Generated from a color scheme\n"
        "composite=true\n"
        "menu_shadow_depth=6\n"
        "tooltip_shadow_depth=4\n"
        "spread_menuitems=true\n"
        "menu_separator_height=7\n"
        "scroll_width=%d\n"
        "scroll_min_extent=40\n"
        "scroll_arrows=false\n"
        "transient_scrollbar=true\n"
        "transient_groove=true\n"
        "slider_width=%d\n"
        "slider_handle_width=%d\n"
        "slider_handle_length=%d\n"
        "check_size=%d\n"
        "combo_as_lineedit=true\n"
        "inline_spin_indicators=true\n"
        "animate_states=true\n"
        "attach_active_tab=true\n"
        "left_tabs=false\n"
        "spread_progressbar=true\n"
        "progressbar_thickness=6\n"
        "button_contents_shift=false\n"
        "splitter_width=6\n"
        "x11drag=menubar_and_primary_toolbar\n"
        "respect_DE=true\n\n"
        "[GeneralColors]\n"
        "window.color=%s\nbase.color=%s\nalt.base.color=%s\nbutton.color=%s\n"
        "light.color=%s\nmid.light.color=%s\ndark.color=%s\nmid.color=%s\n"
        "highlight.color=%s\ninactive.highlight.color=%s\n"
        "text.color=%s\nwindow.text.color=%s\nbutton.text.color=%s\n"
        "disabled.text.color=%s\ntooltip.text.color=%s\nhighlight.text.color=%s\n"
        "link.color=%s\nlink.visited.color=%s\nprogress.indicator.text.color=%s\n\n"
        "[Hacks]\n"
        "respect_darkness=true\n"
        "transparent_dolphin_view=false\n"
        "lxqtmainmenu_iconsize=22\n\n"
        "[PanelButtonCommand]\n"
        "frame=true\nframe.element=button\ninterior=true\ninterior.element=button\n"
        "frame.top=%d\nframe.bottom=%d\nframe.left=%d\nframe.right=%d\n"
        "text.margin.top=3\ntext.margin.bottom=3\ntext.margin.left=8\ntext.margin.right=8\n"
        "text.iconspacing=4\nindicator.element=arrow\nindicator.size=10\n"
        "text.normal.color=%s\ntext.focus.color=%s\ntext.press.color=%s\n"
        "text.toggle.color=%s\ntext.shadow=0\n"
        "min_width=+0.3font\nmin_height=+0.3font\n\n"
        "[PanelButtonTool]\ninherits=PanelButtonCommand\n"
        "frame.element=tbutton\ninterior.element=tbutton\n\n"
        "[ToolbarButton]\ninherits=PanelButtonTool\n\n"
        "[DropDownButton]\ninherits=PanelButtonCommand\nindicator.element=arrow-down\n\n"
        "[IndicatorArrow]\nindicator.element=arrow\nindicator.size=10\n\n"
        "[IndicatorSpinBox]\ninherits=PanelButtonCommand\nframe=false\ninterior=false\n"
        "indicator.element=arrow\n\n"
        "[ComboBox]\ninherits=PanelButtonCommand\n\n"
        "[LineEdit]\ninherits=PanelButtonCommand\n"
        "frame.element=lineedit\ninterior.element=lineedit\n"
        "text.margin.left=6\ntext.margin.right=6\n\n"
        "[CheckBox]\ninherits=PanelButtonCommand\nframe=false\n"
        "interior.element=checkbox\ntext.margin.left=4\n\n"
        "[RadioButton]\ninherits=PanelButtonCommand\nframe=false\n"
        "interior.element=radio\ntext.margin.left=4\n\n"
        "[Menu]\ninherits=PanelButtonCommand\n"
        "frame.element=menu\ninterior.element=menu\n"
        "frame.top=%d\nframe.bottom=%d\nframe.left=%d\nframe.right=%d\n\n"
        "[MenuItem]\ninherits=PanelButtonCommand\n"
        "frame.element=menuitem\ninterior.element=menuitem\nindicator.element=menuitem\n"
        "frame.top=%d\nframe.bottom=%d\nframe.left=%d\nframe.right=%d\n"
        "text.margin.top=3\ntext.margin.bottom=3\ntext.margin.left=6\ntext.margin.right=6\n"
        "text.focus.color=%s\ntext.press.color=%s\ntext.toggle.color=%s\n\n"
        "[MenuBar]\nframe=false\ninterior=false\n\n"
        "[MenuBarItem]\ninherits=PanelButtonCommand\n"
        "frame.element=menubaritem\ninterior.element=menubaritem\n"
        "frame.top=%d\nframe.bottom=%d\nframe.left=%d\nframe.right=%d\n"
        "text.margin.left=6\ntext.margin.right=6\n"
        "text.focus.color=%s\ntext.press.color=%s\ntext.toggle.color=%s\n\n"
        "[ItemView]\ninherits=PanelButtonCommand\n"
        "frame.element=itemview\ninterior.element=itemview\n"
        "frame.top=%d\nframe.bottom=%d\nframe.left=%d\nframe.right=%d\n"
        "text.margin.top=2\ntext.margin.bottom=2\ntext.margin.left=4\ntext.margin.right=4\n"
        "text.focus.color=%s\ntext.press.color=%s\ntext.toggle.color=%s\n\n"
        "[HeaderSection]\ninherits=PanelButtonTool\n\n"
        "[Tab]\ninherits=PanelButtonCommand\nframe.element=tab\ninterior.element=tab\n"
        "text.margin.left=10\ntext.margin.right=10\n"
        "text.toggle.color=%s\ntext.press.color=%s\n\n"
        "[TabFrame]\ninherits=PanelButtonCommand\nframe.element=tabframe\ninterior=false\n\n"
        "[TabBarFrame]\nframe=false\ninterior=false\n\n"
        "[GenericFrame]\ninherits=PanelButtonCommand\nframe.element=common\ninterior=false\n\n"
        "[GroupBox]\ninherits=GenericFrame\n\n"
        "[ToolTip]\ninherits=PanelButtonCommand\nframe.element=tooltip\ninterior.element=tooltip\n"
        "frame.top=%d\nframe.bottom=%d\nframe.left=%d\nframe.right=%d\n"
        "text.margin.left=4\ntext.margin.right=4\n\n"
        "[ScrollbarSlider]\ninherits=PanelButtonCommand\n"
        "frame.element=scrollbarslider\ninterior.element=scrollbarslider\n"
        "frame.top=%d\nframe.bottom=%d\nframe.left=%d\nframe.right=%d\n\n"
        "[ScrollbarTransientSlider]\ninherits=ScrollbarSlider\n\n"
        "[ScrollbarGroove]\ninherits=PanelButtonCommand\n"
        "frame.element=scrollbargroove\ninterior.element=scrollbargroove\n\n"
        "[Scrollbar]\ninherits=PanelButtonCommand\nindicator.element=arrow\nframe=false\n"
        "interior=false\n\n"
        "[Slider]\ninherits=PanelButtonCommand\nframe.element=slider\ninterior.element=slider\n"
        "frame.top=%d\nframe.bottom=%d\nframe.left=%d\nframe.right=%d\n\n"
        "[SliderCursor]\ninherits=PanelButtonCommand\nframe=false\n"
        "interior.element=slidercursor\n\n"
        "[Progressbar]\ninherits=PanelButtonCommand\nframe.element=progress\n"
        "interior.element=progress\nframe.top=3\nframe.bottom=3\nframe.left=3\nframe.right=3\n"
        "text.margin=0\n\n"
        "[ProgressbarContents]\ninherits=Progressbar\nframe.element=progress-pattern\n"
        "interior.element=progress-pattern\n\n"
        "[TreeExpander]\ninherits=PanelButtonCommand\nframe=false\ninterior=false\n"
        "indicator.element=arrow\nindicator.size=10\n\n"
        "[Splitter]\nframe=false\ninterior=false\n\n"
        "[StatusBar]\nframe=false\ninterior=false\n\n"
        "[Toolbar]\nframe=false\ninterior=false\n\n"
        "[Dock]\nframe=false\ninterior=false\n\n"
        "[DockTitle]\nframe=false\ninterior=false\n\n"
        "[Window]\ninterior=false\n",
        name, SCROLL_WIDTH, SLIDER_WIDTH, CHECK_SIZE, CHECK_SIZE, CHECK_SIZE,
        window, base, alt, window, window, alt, border, border, accent, accent,
        text, text, text, dim, text, accent_fg, link, visited, accent_fg,
        fb, fb, fb, fb, text, text, text, text,
        fm, fm, fm, fm,
        fi, fi, fi, fi, accent_fg, accent_fg, accent_fg,
        fi, fi, fi, fi, accent_fg, accent_fg, accent_fg,
        fi, fi, fi, fi, text, accent_fg, accent_fg,
        text, text,
        fm, fm, fm, fm,
        SCROLL_WIDTH / 2, SCROLL_WIDTH / 2, SCROLL_WIDTH / 2, SCROLL_WIDTH / 2,
        SLIDER_WIDTH / 2, SLIDER_WIDTH / 2, SLIDER_WIDTH / 2, SLIDER_WIDTH / 2);
}

gboolean lxp_kvantum_generate(const LxpSchemeColors *colors, int radius, const char *name,
                              const char *dir, GError **error)
{
    g_autofree char *svg = build_svg(colors, radius);
    g_autofree char *kvconfig = build_kvconfig(colors, name, radius);
    g_autofree char *svg_file = g_strconcat(name, ".svg", NULL);
    g_autofree char *config_file = g_strconcat(name, ".kvconfig", NULL);
    g_autofree char *svg_path = g_build_filename(dir, svg_file, NULL);
    g_autofree char *config_path = g_build_filename(dir, config_file, NULL);

    return lxp_write_atomic(svg_path, svg, -1, 0644, error) &&
           lxp_write_atomic(config_path, kvconfig, -1, 0644, error);
}
