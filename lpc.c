// SPDX-License-Identifier: Apache-2.0
/* Copyright 2018-2019 IBM Corp. */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "lpc.h"

#define SYSFS_PREFIX "/sys/kernel/debug/powerpc/lpc"

int lpc_init(struct lpc *ctx, const char *space)
{
    char pathbuf[PATH_MAX];
    int rc;

    assert(ctx);
    assert(space);

    rc = snprintf(pathbuf, sizeof(pathbuf), "%s/%s", SYSFS_PREFIX, space);
    if (rc < 0)
        return rc;

    ctx->fd = open(pathbuf, O_RDWR);
    if (ctx->fd == -1)
        return -errno;

    return 0;
}

int lpc_destroy(struct lpc *ctx)
{
    int rc;

    assert(ctx);

    rc = close(ctx->fd);
    if (rc == -1)
        return -errno;

    return 0;
}

int lpc_read(struct lpc *ctx, size_t addr, void *val, size_t size)
{
    off_t seek_rc;
    ssize_t rc;

    seek_rc = lseek(ctx->fd, addr, SEEK_SET);
    if (seek_rc == (off_t) -1)
        return -errno;

    rc = read(ctx->fd, val, size);
    if (rc == -1)
        return -errno;

    return rc;
}

int lpc_write(struct lpc *ctx, size_t addr, const void *val, size_t size)
{
    off_t seek_rc;
    ssize_t rc;

    seek_rc = lseek(ctx->fd, addr, SEEK_SET);
    if (seek_rc == (off_t) -1)
        return -errno;

    rc = write(ctx->fd, val, size);
    if (rc == -1)
        return -errno;

    return rc;
}

int lpc_readb(struct lpc *ctx, size_t addr, uint8_t *val)
{
    int rc;

    rc = lpc_read(ctx, addr, val, sizeof(*val));

    return rc < 0 ? rc : 0;
}

int lpc_writeb(struct lpc *ctx, size_t addr, uint8_t val)
{
    int rc;

    rc = lpc_write(ctx, addr, &val, sizeof(val));

    return rc < 0 ? rc : 0;
}

int lpc_readw(struct lpc *ctx, size_t addr, uint16_t *val)
{
    int rc;

    rc = lpc_read(ctx, addr, (unsigned char *) val, sizeof(*val));

    return rc < 0 ? rc : 0;
}

int lpc_writew(struct lpc *ctx, size_t addr, uint16_t val)
{
    int rc;

    rc = lpc_write(ctx, addr, (const unsigned char *)&val, sizeof(val));

    return rc < 0 ? rc : 0;
}

int lpc_readl(struct lpc *ctx, size_t addr, uint32_t *val)
{
    int rc;

    rc = lpc_read(ctx, addr, (unsigned char *)val, sizeof(*val));

    return rc < 0 ? rc : 0;
}

int lpc_writel(struct lpc *ctx, size_t addr, uint32_t val)
{
    int rc;

    rc = lpc_write(ctx, addr, (const unsigned char *)&val, sizeof(val));

    return rc < 0 ? rc : 0;
}
