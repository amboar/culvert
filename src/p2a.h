/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _P2A_H
#define _P2A_H

#include "ccan/container_of/container_of.h"

#include "ahb.h"

#include <stdint.h>
#include <sys/types.h>

#define AST_PCI_VID     0x1a03
#define AST_PCI_DID_VGA 0x2000
#define AST_PCI_DID_BMC 0x2402

struct p2ab {
    struct ahb ahb;
    int res;
    void *mmio;
    uint32_t rbar;
};
#define to_p2ab(ahb) container_of(ahb, struct p2ab, ahb)

int p2ab_init(struct p2ab *p2ab, uint16_t vid, uint16_t did);
int p2ab_destroy(struct p2ab *p2ab);
int p2ab_probe(struct p2ab *p2ab);

int64_t p2ab_map(struct p2ab *p2ab, uint32_t phys, size_t len);

static inline struct ahb *p2ab_as_ahb(struct p2ab *ctx)
{
    return &ctx->ahb;
}

ssize_t p2ab_read(struct ahb *ahb, uint32_t phys, void *buf, size_t len);
ssize_t p2ab_write(struct ahb *ahb, uint32_t phys, const void *buf, size_t len);

int p2ab_readl(struct ahb *ahb, uint32_t phys, uint32_t *val);
int p2ab_writel(struct ahb *ahb, uint32_t phys, uint32_t val);

#endif
