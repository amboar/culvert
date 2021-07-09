// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "ahb.h"
#include "ast.h"
#include "devmem.h"
#include "log.h"
#include "mb.h"
#include "mmio.h"
#include "rev.h"

#include <assert.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define AST_SOC_IO	0x1e600000
#define AST_SOC_IO_LEN	0x00200000

int devmem_init(struct devmem *ctx)
{
    int cleanup;
    int rc;

    ctx->pgsize = sysconf(_SC_PAGE_SIZE);

    ctx->fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (ctx->fd < 0)
        return -errno;

    ctx->io = mmap(NULL, 0x00200000, PROT_READ | PROT_WRITE, MAP_SHARED,
                   ctx->fd, 0x1e600000);
    if (ctx->io == MAP_FAILED) { rc = -errno; goto cleanup_fd; }

    ctx->win = NULL;

    return 0;

cleanup_fd:
    cleanup = close(ctx->fd);
    if (cleanup < 0) { perror("close"); }

    return rc;
}

int devmem_destroy(struct devmem *ctx)
{
    int rc;

    assert(!ctx->win);

    rc = munmap(ctx->io, 0x00200000);
    if (rc < 0) { perror("munmap"); }

    rc = close(ctx->fd);
    if (rc < 0) { perror("close"); }

    return 0;
}

int devmem_probe(struct devmem *ctx)
{
    struct ahb ahb;
    int rc;

    logd("Probing %s\n", ahb_interface_names[ahb_devmem]);

    rc = rev_probe(ahb_use(&ahb, ahb_devmem, ctx));

    return rc < 0 ? rc : 1;
}

static int64_t devmem_setup_win(struct devmem *ctx, uint32_t phys, size_t len)
{
    uint32_t aligned = phys & ~(ctx->pgsize - 1);
    uint32_t offset = phys & (ctx->pgsize - 1);
    int rc;

    if (ctx->win && !(ctx->phys == aligned && ctx->len >= offset + len)) {
        rc = munmap(ctx->win, ctx->len);
        if (rc < 0)
            goto cleanup;

        ctx->win = NULL;
    }

    if (!ctx->win) {
        ctx->win = mmap(NULL, offset + len, PROT_READ | PROT_WRITE, MAP_SHARED,
                        ctx->fd, aligned);
        if (ctx->win == MAP_FAILED)
            goto cleanup;

        ctx->phys = aligned;
        ctx->len = offset + len;
    }

    return offset;

cleanup:
    ctx->win = NULL;
    ctx->phys = 0;
    ctx->len = 0;

    return -errno;
}

int devmem_read(struct devmem *ctx, uint32_t phys, void *buf, size_t len)
{
    int64_t woff;

    woff = devmem_setup_win(ctx, phys, len);
    if (woff < 0)
        return woff;

    mmio_memcpy(buf, ctx->win + woff, len);

    return len;
}

int devmem_write(struct devmem *ctx, uint32_t phys, const void *buf, size_t len)
{
    int64_t woff;

    woff = devmem_setup_win(ctx, phys, len);
    if (woff < 0)
        return woff;

    mmio_memcpy(ctx->win + woff, buf, len);

    return len;
}

int devmem_readl(struct devmem *ctx, uint32_t phys, uint32_t *val)
{
    uint32_t container;

    if (phys & 0x3)
        return -EINVAL;

    if (phys >= AST_SOC_IO && phys < (AST_SOC_IO + AST_SOC_IO_LEN)) {
        off_t offset = phys - AST_SOC_IO;
        container = *((volatile uint32_t *)(((char *)ctx->io) + offset));
    } else {
        int64_t woff;

        woff = devmem_setup_win(ctx, phys, sizeof(*val));
        if (woff < 0)
            return woff;

        container = *((volatile uint32_t *)(((char *)ctx->win) + woff));
    }

    *val = le32toh(container);

    return 0;
}

int devmem_writel(struct devmem *ctx, uint32_t phys, uint32_t val)
{
    if (phys & 0x3)
        return -EINVAL;

    val = htole32(val);

    if (phys >= AST_SOC_IO && phys < (AST_SOC_IO + AST_SOC_IO_LEN)) {
        off_t offset = phys - AST_SOC_IO;
        *(volatile uint32_t *)(((char *)ctx->io) + offset) = val;
    } else {
        int64_t woff;

        woff = devmem_setup_win(ctx, phys, sizeof(val));
        if (woff < 0)
            return woff;

        *(volatile uint32_t *)(((char *)ctx->win) + woff) = val;
    }

    iob();

    return 0;
}
