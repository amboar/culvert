/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _AHB_H
#define _AHB_H

#include "log.h"
#include "bridge.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

struct ahb_range {
    const char *name;
    uint32_t start;
    uint64_t len;
    bool rw;
};

struct ahb;

struct ahb_ops {
    ssize_t (*read)(struct ahb *ctx, uint32_t phys, void *buf, size_t len);
    ssize_t (*write)(struct ahb *ctx, uint32_t phys, const void *buf, size_t len);
    int (*readl)(struct ahb *ctx, uint32_t phys, uint32_t *val);
    int (*writel)(struct ahb *ctx, uint32_t phys, uint32_t val);
};

struct ahb {
    const struct bridge_driver *drv;
    const struct ahb_ops *ops;
};

static inline void ahb_init_ops(struct ahb *ctx, const struct bridge_driver *drv,
                                const struct ahb_ops *ops)
{
    ctx->drv = drv;
    ctx->ops = ops;
}

static inline ssize_t ahb_read(struct ahb *ctx, uint32_t phys, void *buf, size_t len)
{
    return ctx->ops->read(ctx, phys, buf, len);
}

static inline ssize_t ahb_write(struct ahb *ctx, uint32_t phys, const void *buf, size_t len)
{
    return ctx->ops->write(ctx, phys, buf, len);
}

static inline int ahb_readl(struct ahb *ctx, uint32_t phys, uint32_t *val)
{
    int rc = ctx->ops->readl(ctx, phys, val);
    if (!rc) {
        logt("%s: 0x%08"PRIx32": 0x%08"PRIx32"\n", __func__, phys, *val);
    }

    return rc;
}

static inline int ahb_writel(struct ahb *ctx, uint32_t phys, uint32_t val)
{
    int rc = ctx->ops->writel(ctx, phys, val);
    if (!rc) {
        logt("%s: 0x%08"PRIx32": 0x%08"PRIx32"\n", __func__, phys, val);
    }

    return rc;
}


ssize_t ahb_siphon_in(struct ahb *ctx, uint32_t phys, size_t len, int outfd);
ssize_t ahb_siphon_out(struct ahb *ctx, uint32_t phys, int infd);

#endif
