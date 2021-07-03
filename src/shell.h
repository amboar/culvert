/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _SHELL_H
#define _SHELL_H

#include <stddef.h>

/* A bit sketchy, might eventually need a dynamically sized buffer */
ssize_t shell_get_output(const char *cmd, char *buf, size_t len);

#endif
