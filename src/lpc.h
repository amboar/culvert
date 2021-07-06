/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef LPC_H
#define LPC_H

#include "config.h"
#include "compiler.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

struct lpc
{
    int fd;
    const char *space;
};

#if HAVE_LPC
int lpc_init(struct lpc *ctx, const char *space);
int lpc_destroy(struct lpc *ctx);

int lpc_readb(struct lpc *ctx, size_t addr, uint8_t *val);
int lpc_writeb(struct lpc *ctx, size_t addr, uint8_t val);
int lpc_readw(struct lpc *ctx, size_t addr, uint16_t *val);
int lpc_writew(struct lpc *ctx, size_t addr, uint16_t val);
int lpc_readl(struct lpc *ctx, size_t addr, uint32_t *val);
int lpc_writel(struct lpc *ctx, size_t addr, uint32_t val);

int lpc_read(struct lpc *ctx, size_t addr, void *val, size_t size);
int lpc_write(struct lpc *ctx, size_t addr, const void *val, size_t size);
#else
static inline int
lpc_init(struct lpc *ctx __unused, const char *space __unused)
{
    return -ENOTSUP;
}

static inline int
lpc_destroy(struct lpc *ctx __unused)
{
    return -ENOTSUP;
}

static inline int
lpc_readb(struct lpc *ctx __unused, size_t addr __unused, uint8_t *val __unused)
{
    return -ENOTSUP;
}

static inline int
lpc_writeb(struct lpc *ctx __unused, size_t addr __unused, uint8_t val __unused)
{
    return -ENOTSUP;
}

static inline int
lpc_readw(struct lpc *ctx __unused, size_t addr __unused,
          uint16_t *val __unused)
{
    return -ENOTSUP;
}

static inline int
lpc_writew(struct lpc *ctx __unused, size_t addr __unused,
           uint16_t val __unused)
{
    return -ENOTSUP;
}

static inline int
lpc_readl(struct lpc *ctx __unused, size_t addr __unused,
          uint32_t *val __unused)
{
    return -ENOTSUP;
}

static inline int
lpc_writel(struct lpc *ctx __unused, size_t addr __unused,
           uint32_t val __unused)
{
    return -ENOTSUP;
}

static inline int
lpc_read(struct lpc *ctx __unused, size_t addr __unused, void *val __unused,
         size_t size __unused)
{
    return -ENOTSUP;
}

static inline int
lpc_write(struct lpc *ctx __unused, size_t addr __unused, const void *val
          __unused, size_t size __unused)
{
    return -ENOTSUP;
}
#endif

#endif
