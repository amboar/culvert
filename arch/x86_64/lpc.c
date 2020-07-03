// SPDX-License-Identifier: Apache-2.0
/* Copyright 2014-2016 IBM Corp. */

#include "lpc.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/io.h>

int lpc_init(struct lpc *ctx, const char *space)
{
    int rc;

    if (strcmp(space, "io"))
        return -ENOTSUP;

    /* YOLO */
    rc = iopl(3);
    if (rc < 0) {
        perror("iopl");
        return rc;
    }

    return 0;
}

int lpc_destroy(struct lpc *ctx)
{
    return 0;
}

int lpc_readb(struct lpc *ctx, size_t addr, uint8_t *val)
{
    *val = inb_p(addr);

    return 0;
}

int lpc_writeb(struct lpc *ctx, size_t addr, uint8_t val)
{
    outb_p(val, addr);

    return 0;
}

int lpc_readw(struct lpc *ctx, size_t addr, uint16_t *val)
{
    *val = inw_p(addr);

    return 0;
}

int lpc_writew(struct lpc *ctx, size_t addr, uint16_t val)
{
    outw_p(val, addr);

    return 0;
}

int lpc_readl(struct lpc *ctx, size_t addr, uint32_t *val)
{
    *val = inl_p(addr);

    return 0;
}

int lpc_writel(struct lpc *ctx, size_t addr, uint32_t val)
{
    outl_p(val, addr);

    return 0;
}

int lpc_read(struct lpc *ctx, size_t addr, void *val, size_t size)
{
    return -ENOTSUP;
}

int lpc_write(struct lpc *ctx, size_t addr, const void *val, size_t size)
{
    return -ENOTSUP;
}
