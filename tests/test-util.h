/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "lxp-fileutil.h"

#include <glib.h>

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
