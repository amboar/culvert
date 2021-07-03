/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2020 IBM Corp. */

#ifndef _CONSOLE_H
#define _CONSOLE_H

struct console;

struct console_ops {
    int (*destroy)(struct console *ctx);
    int (*set_baud)(struct console *ctx, int baud);
};

struct console {
    const struct console_ops *ops;
};

static inline int console_destroy(struct console *ctx)
{
    return ctx->ops->destroy(ctx);
}

static inline int console_set_baud(struct console *ctx, int baud)
{
    return ctx->ops->set_baud(ctx, baud);
}

#endif
