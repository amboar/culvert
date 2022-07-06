/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef ILPC_H
#define ILPC_H

#include "ccan/container_of/container_of.h"

#include "ahb.h"
#include "sio.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct ilpcb
{
    struct ahb ahb;
    struct sio sio;
};
#define to_ilpcb(ahb) container_of(ahb, struct ilpcb, ahb)

int ilpcb_init(struct ilpcb *ctx);
int ilpcb_destroy(struct ilpcb *ctx);
int ilpcb_probe(struct ilpcb *ctx);

static inline struct ahb *ilpcb_as_ahb(struct ilpcb *ctx)
{
    return &ctx->ahb;
}

/* These are going to be *amazingly* slow. Use the l2ab */
ssize_t ilpcb_read(struct ahb *ahb, uint32_t addr, void *buf, size_t len);
ssize_t ilpcb_write(struct ahb *ahb, uint32_t addr, const void *buf, size_t len);

int ilpcb_readl(struct ahb *ahb, uint32_t addr, uint32_t *val);
int ilpcb_writel(struct ahb *ahb, uint32_t addr, uint32_t val);

#endif
