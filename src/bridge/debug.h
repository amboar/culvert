/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _DEBUG_H
#define _DEBUG_H

#include "ahb.h"
#include "console.h"
#include "prompt.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/types.h>

struct debug {
    struct ahb ahb;
    struct console *console;
    struct prompt prompt;
    int port;
};

int debug_init(struct debug *ctx, ...);
int debug_init_v(struct debug *ctx, va_list args);
int debug_destroy(struct debug *ctx);

int debug_enter(struct debug *ctx);
int debug_exit(struct debug *ctx);
int debug_probe(struct debug *ctx);

static inline struct ahb *debug_as_ahb(struct debug *ctx)
{
    return &ctx->ahb;
}

ssize_t debug_read(struct ahb *ahb, uint32_t phys, void *buf, size_t len);
ssize_t debug_write(struct ahb *ahb, uint32_t phys, const void *buf, size_t len);
int debug_readl(struct ahb *ahb, uint32_t phys, uint32_t *val);
int debug_writel(struct ahb *ahb, uint32_t phys, uint32_t val);

#endif
