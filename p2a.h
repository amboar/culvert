/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _P2A_H
#define _P2A_H

#include <errno.h>
#include <stdint.h>
#include <sys/types.h>

#define AST_PCI_VID     0x1a03
#define AST_PCI_DID_VGA 0x2000
#define AST_PCI_DID_BMC 0x2402

struct p2ab {
    int res;
    void *mmio;
    uint32_t rbar;
};

static inline int p2ab_init(struct p2ab *p2ab, uint16_t vid, uint16_t did)
{
    return -ENOTSUP;
}

static inline int p2ab_destroy(struct p2ab *p2ab)
{
    return -ENOTSUP;
}

static inline int p2ab_probe(struct p2ab *p2ab)
{
    return -ENOTSUP;
}


static inline int64_t p2ab_map(struct p2ab *p2ab, uint32_t phys, size_t len)
{
    return -ENOTSUP;
}

static inline ssize_t p2ab_read(struct p2ab *ctx, uint32_t phys, void *buf, size_t len)
{
    return -ENOTSUP;
}

static inline ssize_t p2ab_write(struct p2ab *ctx, uint32_t phys, const void *buf, size_t len)
{
    return -ENOTSUP;
}


static inline int p2ab_readl(struct p2ab *ctx, uint32_t phys, uint32_t *val)
{
    return -ENOTSUP;
}

static inline int p2ab_writel(struct p2ab *ctx, uint32_t phys, uint32_t val)
{
    return -ENOTSUP;
}


#endif
