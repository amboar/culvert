/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef ILPC_H
#define ILPC_H

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

struct ilpcb
{
};

static inline int ilpcb_init(struct ilpcb *ctx)
{
    return -ENOTSUP;
}

static inline int ilpcb_destroy(struct ilpcb *ctx)
{
    return -ENOTSUP;
}

static inline int ilpcb_probe(struct ilpcb *ctx)
{
    return -ENOTSUP;
}

/* These are going to be *amazingly* slow. Use the l2ab */
static inline int ilpcb_read(struct ilpcb *ctx, size_t addr, void *buf, size_t len)
{
    return -ENOTSUP;
}

static inline int ilpcb_write(struct ilpcb *ctx, size_t addr, const void *buf, size_t len)
{
    return -ENOTSUP;
}

static inline int ilpcb_readl(struct ilpcb *ctx, size_t addr, uint32_t *val)
{
    return -ENOTSUP;
}

static inline int ilpcb_writel(struct ilpcb *ctx, size_t addr, uint32_t val)
{
    return -ENOTSUP;
}

#endif
