/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef L2A_H
#define L2A_H

#include "ccan/container_of/container_of.h"

#include "ahb.h"
#include "lpc.h"
#include "ilpc.h"

#include <stdint.h>
#include <sys/types.h>

struct l2ab {
    struct ahb ahb;
    struct lpc fw;
    struct ilpcb ilpcb;
    uint32_t phys;
    size_t len;
    uint32_t restore7;
    uint32_t restore8;
};
#define to_l2ab(ahb) container_of(ahb, struct l2ab, ahb)

int l2ab_init(struct l2ab *ctx);
int l2ab_destroy(struct l2ab *ctx);

int64_t l2ab_map(struct l2ab *ctx, uint32_t phys, size_t len);

static inline struct ahb *l2ab_as_ahb(struct l2ab *ctx)
{
    return &ctx->ahb;
}

ssize_t l2ab_read(struct ahb *ahb, uint32_t phys, void *buf, size_t len);
ssize_t l2ab_write(struct ahb *ahb, uint32_t phys, const void *buf, size_t len);

int l2ab_readl(struct ahb *ahb, uint32_t phys, uint32_t *val);
int l2ab_writel(struct ahb *ahb, uint32_t phys, uint32_t val);

#endif
