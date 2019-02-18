/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _PRIV_H
#define _PRIV_H

#include <stdbool.h>

bool priv_am_root(void);
void priv_print_unprivileged(const char *name);

#endif
