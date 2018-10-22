/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef ILPC_H
#define ILPC_H

#include <stddef.h>
#include <stdint.h>

#include "sio.h"

struct ilpcb
{
    struct sio sio;
};

int ilpcb_init(struct ilpcb *ctx);
int ilpcb_destroy(struct ilpcb *ctx);
int ilpcb_probe(struct ilpcb *ctx);

/* These are going to be *amazingly* slow. Use the l2ab */
int ilpcb_read(struct ilpcb *ctx, size_t addr, void *buf, size_t len);
int ilpcb_write(struct ilpcb *ctx, size_t addr, const void *buf, size_t len);

int ilpcb_readl(struct ilpcb *ctx, size_t addr, uint32_t *val);
int ilpcb_writel(struct ilpcb *ctx, size_t addr, uint32_t val);

#endif
