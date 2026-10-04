/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <glib.h>

#define LXP_ERROR (lxp_error_quark())

typedef enum {
    LXP_ERROR_FAILED,
    LXP_ERROR_NOT_FOUND,
    LXP_ERROR_INVALID,
    LXP_ERROR_EXISTS,
    LXP_ERROR_UNSUPPORTED,
} LxpError;

GQuark lxp_error_quark(void);
