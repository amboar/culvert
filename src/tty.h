/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2020 IBM Corp. */

#ifndef _TTY_H
#define _TTY_H

#include "console.h"

struct tty {
	struct console console;
	int fd;
};

int tty_init(struct tty *ctx, const char *path);

#endif
