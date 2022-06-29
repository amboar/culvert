/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _DEVMEM_H
#define _DEVMEM_H

#include "ahb.h"

#include <stdint.h>
#include <sys/types.h>

struct devmem {
    struct ahb ahb;
    int fd;
    void *io;
    void *win;
    off_t phys;
    size_t len;
    off_t pgsize;
};

int devmem_init(struct devmem *ctx);
int devmem_destroy(struct devmem *ctx);

int devmem_probe(struct devmem *ctx);

static inline struct ahb *devmem_as_ahb(struct devmem *ctx)
{
    return &ctx->ahb;
}

ssize_t devmem_read(struct ahb *ahb, uint32_t phys, void *buf, size_t len);
ssize_t devmem_write(struct ahb *ahb, uint32_t phys, const void *buf, size_t len);

int devmem_readl(struct ahb *ahb, uint32_t phys, uint32_t *val);
int devmem_writel(struct ahb *ahb, uint32_t phys, uint32_t val);

#endif
