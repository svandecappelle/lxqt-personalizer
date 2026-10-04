/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-fileutil.h"

#include <glib.h>
#include <glib/gstdio.h>

/* Copy tests/data/<fixture> to <dest_dir>/<relpath>. */
static inline void install_fixture(const char *fixture, const char *dest_dir,
                                   const char *relpath)
{
    g_autofree char *src = g_build_filename(TEST_DATA_DIR, fixture, NULL);
    g_autofree char *dst = g_build_filename(dest_dir, relpath, NULL);
    g_autoptr(GError) error = NULL;

    lxp_copy_atomic(src, dst, 0644, &error);
    g_assert_no_error(error);
}

/* Create <dir>/<relpath> with <content>. */
static inline void write_file(const char *dir, const char *relpath,
                              const char *content)
{
    g_autofree char *path = g_build_filename(dir, relpath, NULL);
    g_autoptr(GError) error = NULL;

    lxp_write_atomic(path, content, -1, 0644, &error);
    g_assert_no_error(error);
}

static inline char *read_file(const char *dir, const char *relpath)
{
    g_autofree char *path = g_build_filename(dir, relpath, NULL);
    char *data = NULL;

    g_file_get_contents(path, &data, NULL, NULL);
    return data;
}

/*
 * Put fake executables first in PATH so backend availability does not
 * depend on what is installed on the machine running the tests.
 */
static inline void fake_programs(const char *const *names)
{
    g_autofree char *bin = g_build_filename(g_get_user_data_dir(), "fake-bin", NULL);

    for (const char *const *n = names; *n; n++) {
        g_autofree char *path = g_build_filename(bin, *n, NULL);
        write_file(bin, *n, "#!/bin/sh\nexit 0\n");
        g_chmod(path, 0755);
    }
    g_setenv("PATH", bin, TRUE);
}
