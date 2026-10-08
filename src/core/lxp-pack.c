/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lxp-pack.h"

#include "lxp-assets.h"
#include "lxp-error.h"
#include "lxp-fileutil.h"
#include "lxp-paths.h"
#include "lxp-system.h"

#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_ENTRIES     20000
#define MAX_FILE_SIZE   (256 * 1024 * 1024)
#define MAX_TOTAL_SIZE  (512 * 1024 * 1024)
#define MAX_BUNDLE_SIZE (64 * 1024 * 1024)

/* --- export ------------------------------------------------------------- */

static gboolean is_user_path(const char *path)
{
    const char *home = g_get_home_dir();
    const char *data = g_get_user_data_dir();

    return (g_str_has_prefix(path, home) && path[strlen(home)] == G_DIR_SEPARATOR) ||
           (g_str_has_prefix(path, data) && path[strlen(data)] == G_DIR_SEPARATOR);
}

static goffset tree_size(const char *path)
{
    g_autoptr(GDir) dir = NULL;
    const char *name;
    GStatBuf st;
    goffset total = 0;

    if (g_lstat(path, &st) != 0)
        return 0;
    if (!S_ISDIR(st.st_mode))
        return S_ISREG(st.st_mode) ? st.st_size : 0;
    if (!(dir = g_dir_open(path, 0, NULL)))
        return 0;
    while ((name = g_dir_read_name(dir))) {
        g_autofree char *child = g_build_filename(path, name, NULL);
        total += tree_size(child);
    }
    return total;
}

/* Copy the user-installed theme <name> of <kind> into the staging data/. */
static gboolean bundle_one(const char *stage, const char *kind, const char *name,
                           GPtrArray *notes, GError **error)
{
    g_autofree char *dest = g_build_filename(stage, "data", kind, name, NULL);
    g_autofree char *src = NULL;
    g_autofree char *marker = NULL;

    if (!name || !*name || strchr(name, G_DIR_SEPARATOR) ||
        g_file_test(dest, G_FILE_TEST_EXISTS))
        return TRUE;  /* already bundled (e.g. generated) */

    src = lxp_find_installed(kind, name, NULL);
    if (!src || !is_user_path(src))
        return TRUE;  /* system theme: provided by packages */
    if (tree_size(src) > MAX_BUNDLE_SIZE) {
        if (notes)
            g_ptr_array_add(notes, g_strdup_printf("%s “%s” is too large to bundle", kind, name));
        return TRUE;
    }

    marker = g_build_filename(dest, LXP_ASSET_MARKER, NULL);
    if (!lxp_copy_tree(src, dest, error) ||
        !lxp_write_atomic(marker, LXP_ASSET_IF_MISSING, -1, 0644, error))
        return FALSE;
    if (notes)
        g_ptr_array_add(notes, g_strdup_printf("Bundled %s “%s”", kind, name));
    return TRUE;
}

static gboolean bundle_assets(const LxpTheme *theme, const char *stage, GPtrArray *notes,
                              GError **error)
{
    g_autofree char *openbox = lxp_theme_get_string(theme, "Openbox", "theme");
    g_autofree char *gtk = lxp_theme_get_string(theme, "GTK", "theme");
    g_autofree char *lxqt = lxp_theme_get_string(theme, "LXQt", "theme");

    return bundle_one(stage, "themes", openbox, notes, error) &&
           bundle_one(stage, "themes", gtk, notes, error) &&
           bundle_one(stage, "lxqt/themes", lxqt, notes, error);
}

static int compare_names(gconstpointer a, gconstpointer b)
{
    return g_strcmp0(*(char *const *)a, *(char *const *)b);
}

static gboolean archive_fail(struct archive *a, const char *what, GError **error)
{
    g_set_error(error, LXP_ERROR, LXP_ERROR_FAILED, "%s: %s", what,
                archive_error_string(a) ? archive_error_string(a) : "archive error");
    return FALSE;
}

/* Add <path> as <name> (recursively, sorted for reproducible archives). */
static gboolean add_tree(struct archive *a, const char *path, const char *name,
                         GError **error)
{
    struct archive_entry *entry;
    GStatBuf st;
    gboolean ok = TRUE;

    if (g_lstat(path, &st) != 0 || (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)))
        return TRUE;  /* links and specials are not exported */

    entry = archive_entry_new();
    archive_entry_set_pathname(entry, name);
    archive_entry_set_filetype(entry, S_ISDIR(st.st_mode) ? AE_IFDIR : AE_IFREG);
    archive_entry_set_perm(entry, S_ISDIR(st.st_mode) ? 0755 : 0644);
    archive_entry_set_size(entry, S_ISDIR(st.st_mode) ? 0 : st.st_size);
    archive_entry_set_mtime(entry, st.st_mtime, 0);
    if (archive_write_header(a, entry) != ARCHIVE_OK)
        ok = archive_fail(a, name, error);
    archive_entry_free(entry);
    if (!ok)
        return FALSE;

    if (S_ISREG(st.st_mode)) {
        g_autofree char *data = NULL;
        gsize len;

        if (!g_file_get_contents(path, &data, &len, error))
            return FALSE;
        if (len > 0 && archive_write_data(a, data, len) < 0)
            return archive_fail(a, name, error);
        return TRUE;
    }

    {
        g_autoptr(GDir) dir = g_dir_open(path, 0, error);
        g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func(g_free);
        const char *child;

        if (!dir)
            return FALSE;
        while ((child = g_dir_read_name(dir)))
            g_ptr_array_add(names, g_strdup(child));
        g_ptr_array_sort(names, compare_names);
        for (guint i = 0; i < names->len; i++) {
            g_autofree char *cpath = g_build_filename(path, names->pdata[i], NULL);
            g_autofree char *cname = g_strconcat(name, "/", names->pdata[i], NULL);
            if (!add_tree(a, cpath, cname, error))
                return FALSE;
        }
    }
    return TRUE;
}

gboolean lxp_pack_export(const LxpTheme *theme, const char *archive_path,
                         const char *preview_png, LxpPackFlags flags,
                         GPtrArray *notes, GError **error)
{
    const char *dir = lxp_theme_get_dir(theme);
    g_autofree char *slug = lxp_theme_slugify(lxp_theme_get_name(theme));
    g_autofree char *tmp = NULL;
    g_autofree char *stage = NULL;
    g_autoptr(LxpTheme) copy = NULL;
    struct archive *a = NULL;
    gboolean ok = FALSE;

    if (!dir) {
        g_set_error_literal(error, LXP_ERROR, LXP_ERROR_INVALID, "Save the theme before exporting it");
        return FALSE;
    }
    if (!(tmp = g_dir_make_tmp("lxp-export-XXXXXX", error)))
        return FALSE;
    stage = g_build_filename(tmp, slug, NULL);

    /* Work on a copy: bundling must not change the installed theme. */
    if (!lxp_copy_tree(dir, stage, error) || !(copy = lxp_theme_load(stage, error)) ||
        !lxp_theme_bundle_wallpaper(copy, stage, error) ||
        (!(flags & LXP_PACK_NO_BUNDLE) && !bundle_assets(copy, stage, notes, error)) ||
        !lxp_theme_save(copy, stage, error))
        goto out;
    if (preview_png) {
        g_autofree char *dest = g_build_filename(stage, "preview.png", NULL);
        if (!lxp_copy_atomic(preview_png, dest, 0644, error))
            goto out;
    }

    a = archive_write_new();
    if (archive_write_add_filter_gzip(a) != ARCHIVE_OK ||
        archive_write_set_format_pax_restricted(a) != ARCHIVE_OK ||
        archive_write_open_filename(a, archive_path) != ARCHIVE_OK) {
        archive_fail(a, archive_path, error);
        goto out;
    }
    ok = add_tree(a, stage, slug, error);
    if (archive_write_close(a) != ARCHIVE_OK && ok)
        ok = archive_fail(a, archive_path, error);

out:
    if (a)
        archive_write_free(a);
    lxp_remove_tree(tmp, NULL);
    if (!ok)
        g_unlink(archive_path);
    return ok;
}

/* --- import ------------------------------------------------------------- */

/* "./a//b/" -> "a/b"; NULL for absolute paths, "..", or empty names. */
static char *safe_path(const char *raw)
{
    g_auto(GStrv) parts = NULL;
    g_autoptr(GStrvBuilder) clean = g_strv_builder_new();
    g_auto(GStrv) kept = NULL;

    if (!raw || raw[0] == '/' || strchr(raw, '\\'))
        return NULL;
    parts = g_strsplit(raw, "/", -1);
    for (char **p = parts; *p; p++) {
        if (!**p || g_str_equal(*p, "."))
            continue;
        if (g_str_equal(*p, ".."))
            return NULL;
        g_strv_builder_add(clean, *p);
    }
    kept = g_strv_builder_end(clean);
    return kept[0] ? g_strjoinv("/", kept) : NULL;
}

static gboolean write_entry(struct archive *a, const char *path, gint64 size,
                            GError **error)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    char buf[64 * 1024];
    gint64 written = 0;
    la_ssize_t n;

    if (fd < 0) {
        int err = errno;
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
                    "Failed to create %s: %s", path, g_strerror(err));
        return FALSE;
    }
    while ((n = archive_read_data(a, buf, sizeof(buf))) > 0) {
        written += n;
        if (written > size || written > MAX_FILE_SIZE ||
            write(fd, buf, (size_t)n) != n) {
            close(fd);
            g_set_error(error, LXP_ERROR, LXP_ERROR_INVALID, "Bad archive entry %s", path);
            return FALSE;
        }
    }
    close(fd);
    if (n < 0)
        return archive_fail(a, path, error);
    return TRUE;
}

static gboolean extract(const char *archive_path, const char *dest, GError **error)
{
    struct archive *a = archive_read_new();
    struct archive_entry *entry;
    gint64 total = 0;
    int count = 0, r;
    gboolean ok = TRUE;

    archive_read_support_filter_all(a);
    archive_read_support_format_tar(a);
    if (archive_read_open_filename(a, archive_path, 64 * 1024) != ARCHIVE_OK) {
        ok = archive_fail(a, archive_path, error);
        goto out;
    }

    while (ok && (r = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        g_autofree char *rel = safe_path(archive_entry_pathname(entry));
        g_autofree char *path = NULL;
        mode_t type = archive_entry_filetype(entry);
        gint64 size = archive_entry_size(entry);

        if (++count > MAX_ENTRIES || !rel || (type != AE_IFREG && type != AE_IFDIR) ||
            size < 0 || size > MAX_FILE_SIZE || (total += size) > MAX_TOTAL_SIZE) {
            g_set_error(error, LXP_ERROR, LXP_ERROR_INVALID,
                        "Refusing unsafe archive entry “%s”",
                        archive_entry_pathname(entry) ? archive_entry_pathname(entry) : "?");
            ok = FALSE;
            break;
        }

        path = g_build_filename(dest, rel, NULL);
        if (type == AE_IFDIR) {
            if (g_mkdir_with_parents(path, 0755) != 0) {
                g_set_error(error, LXP_ERROR, LXP_ERROR_FAILED, "Failed to create %s", path);
                ok = FALSE;
            }
            continue;
        }
        ok = lxp_ensure_parent_dir(path, error) && write_entry(a, path, size, error);
    }
    if (ok && r != ARCHIVE_EOF)
        ok = archive_fail(a, archive_path, error);

out:
    archive_read_free(a);
    return ok;
}

/* The directory holding theme.ini: <dir> itself or its only subdirectory. */
static char *find_root(const char *dir, GError **error)
{
    g_autofree char *manifest = g_build_filename(dir, LXP_THEME_MANIFEST, NULL);
    g_autoptr(GDir) d = NULL;
    const char *name, *only = NULL;
    int n = 0;

    if (g_file_test(manifest, G_FILE_TEST_IS_REGULAR))
        return g_strdup(dir);
    if (!(d = g_dir_open(dir, 0, error)))
        return NULL;
    while ((name = g_dir_read_name(d))) {
        n++;
        only = name;
        if (n == 1) {
            g_autofree char *sub = g_build_filename(dir, name, LXP_THEME_MANIFEST, NULL);
            if (!g_file_test(sub, G_FILE_TEST_IS_REGULAR))
                n = 2;
        }
    }
    if (n != 1) {
        g_set_error_literal(error, LXP_ERROR, LXP_ERROR_INVALID,
                            "This file is not a theme pack (no theme.ini)");
        return NULL;
    }
    return g_build_filename(dir, only, NULL);
}

char *lxp_pack_import(const char *archive_path, LxpPackFlags flags, GError **error)
{
    g_autofree char *themes = lxp_paths_themes_dir();
    g_autofree char *tmp = NULL;
    g_autofree char *root = NULL;
    g_autofree char *slug = NULL;
    g_autofree char *dest = NULL;
    g_autoptr(LxpTheme) theme = NULL;
    char *result = NULL;

    /* Extract next to the themes directory so the final move is a rename. */
    if (g_mkdir_with_parents(themes, 0755) != 0) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_FAILED, "Cannot create %s", themes);
        return NULL;
    }
    tmp = g_build_filename(themes, ".import-XXXXXX", NULL);
    if (!g_mkdtemp_full(tmp, 0700)) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_FAILED, "Cannot create a temporary directory");
        return NULL;
    }

    if (!extract(archive_path, tmp, error) || !(root = find_root(tmp, error)) ||
        !(theme = lxp_theme_load(root, error)))
        goto out;

    slug = lxp_theme_slugify(lxp_theme_get_name(theme));
    dest = g_build_filename(themes, slug, NULL);
    if (g_file_test(dest, G_FILE_TEST_EXISTS)) {
        if (!(flags & LXP_PACK_FORCE)) {
            g_set_error(error, LXP_ERROR, LXP_ERROR_EXISTS,
                        "A theme named “%s” is already installed", lxp_theme_get_name(theme));
            goto out;
        }
        if (!lxp_remove_tree(dest, error))
            goto out;
    }
    if (g_rename(root, dest) != 0) {
        g_set_error(error, LXP_ERROR, LXP_ERROR_FAILED, "Failed to install %s", dest);
        goto out;
    }
    result = g_steal_pointer(&dest);

out:
    lxp_remove_tree(tmp, NULL);
    return result;
}
