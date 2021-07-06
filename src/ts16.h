/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2020 IBM Corp. */

#ifndef _TS16_H
#define _TS16_H

#include "console.h"
#include "prompt.h"

struct ts16 {
    struct console console;
    struct prompt concentrator;
    int port;
};

int ts16_init(struct ts16 *ctx, const char *ip, int port, const char *username,
              const char *password);

#endif
