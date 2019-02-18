/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _DEBUG_H
#define _DEBUG_H

#include <errno.h>
#include <stdint.h>
#include <sys/types.h>

struct debug {
    int port;
};

static inline int debug_init(struct debug *ctx, const char *compatible, const char *ip,
               int port, const char *username, const char *password)
{
    return -ENOTSUP;
}

static inline int debug_cleanup(struct debug *ctx)
{
    return -ENOTSUP;
}

static inline int debug_destroy(struct debug *ctx)
{
    return -ENOTSUP;
}

static inline int debug_enter(struct debug *ctx)
{
    return -ENOTSUP;
}

static inline int debug_exit(struct debug *ctx)
{
    return -ENOTSUP;
}

static inline int debug_probe(struct debug *ctx)
{
    return -ENOTSUP;
}

static inline ssize_t debug_read(struct debug *ctx, uint32_t phys, void *buf, size_t len)
{
    return -ENOTSUP;
}

static inline ssize_t debug_write(struct debug *ctx, uint32_t phys, const void *buf, size_t len)
{
    return -ENOTSUP;
}

static inline int debug_readl(struct debug *ctx, uint32_t phys, uint32_t *val)
{
    return -ENOTSUP;
}

static inline int debug_writel(struct debug *ctx, uint32_t phys, uint32_t val)
{
    return -ENOTSUP;
}


#endif
