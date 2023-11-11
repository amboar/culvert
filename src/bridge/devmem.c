// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "ahb.h"
#include "ast.h"
#include "bridge.h"
#include "compiler.h"
#include "devmem.h"
#include "log.h"
#include "mb.h"
#include "mmio.h"
#include "rev.h"

#include "ccan/container_of/container_of.h"

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

#define to_devmem(ahb) container_of(ahb, struct devmem, ahb)

int devmem_probe(struct devmem *ctx)
{
    int rc;

    logd("Probing %s\n", ctx->ahb.drv->name);

#ifndef __arm__
    return -ENOTSUP;
#endif

    rc = rev_probe(devmem_as_ahb(ctx));

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

ssize_t devmem_read(struct ahb *ahb, uint32_t phys, void *buf, size_t len)
{
    struct devmem *ctx = to_devmem(ahb);
    int64_t woff;

    woff = devmem_setup_win(ctx, phys, len);
    if (woff < 0)
        return woff;

    mmio_memcpy(buf, ctx->win + woff, len);

    return len;
}

ssize_t devmem_write(struct ahb *ahb, uint32_t phys, const void *buf, size_t len)
{
    struct devmem *ctx = to_devmem(ahb);
    int64_t woff;

    woff = devmem_setup_win(ctx, phys, len);
    if (woff < 0)
        return woff;

    mmio_memcpy(ctx->win + woff, buf, len);

    return len;
}

int devmem_readl(struct ahb *ahb, uint32_t phys, uint32_t *val)
{
    struct devmem *ctx = to_devmem(ahb);
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

int devmem_writel(struct ahb *ahb, uint32_t phys, uint32_t val)
{
    struct devmem *ctx = to_devmem(ahb);

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

static const struct ahb_ops devmem_ahb_ops = {
    .read = devmem_read,
    .write = devmem_write,
    .readl = devmem_readl,
    .writel = devmem_writel
};

static struct ahb *devmem_driver_probe(int argc, char *argv[]);
static void devmem_driver_destroy(struct ahb *ahb);

static const struct bridge_driver devmem_driver = {
    .name = "devmem",
    .probe = devmem_driver_probe,
    .destroy = devmem_driver_destroy,
    .local = true,
};
REGISTER_BRIDGE_DRIVER(devmem_driver);

int devmem_init(struct devmem *ctx)
{
    int cleanup;
    int rc;

    ctx->pgsize = sysconf(_SC_PAGE_SIZE);

    ctx->fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (ctx->fd < 0)
        return -errno;

    ctx->io = mmap(NULL, AST_SOC_IO_LEN, PROT_READ | PROT_WRITE, MAP_SHARED,
                   ctx->fd, AST_SOC_IO);
    if (ctx->io == MAP_FAILED) { rc = -errno; goto cleanup_fd; }

    ctx->win = NULL;

    ahb_init_ops(&ctx->ahb, &devmem_driver, &devmem_ahb_ops);

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

    rc = munmap(ctx->io, AST_SOC_IO_LEN);
    if (rc < 0) { perror("munmap"); }

    rc = close(ctx->fd);
    if (rc < 0) { perror("close"); }

    return 0;
}

static struct ahb *
devmem_driver_probe(int argc, char *argv[] __unused)
{
    struct devmem *ctx;
    int rc;

    // This driver doesn't require args, so if there are any we're not trying to probe it
    if (argc > 0) {
        return NULL;
    }

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return NULL;
    }

    if ((rc = devmem_init(ctx)) < 0) {
        loge("failed to initialise devmem bridge: %d\n", rc);
        goto cleanup_ctx;
    }

    if ((rc = devmem_probe(ctx)) < 0) {
        loge("Failed devmem probe: %d\n", rc);
        goto destroy_ctx;
    }

    return devmem_as_ahb(ctx);

destroy_ctx:
    devmem_destroy(ctx);

cleanup_ctx:
    free(ctx);

    return NULL;
}

static void devmem_driver_destroy(struct ahb *ahb)
{
    struct devmem *ctx = to_devmem(ahb);
    int rc;

    if ((rc = devmem_destroy(ctx)) < 0) {
        loge("Failed to destroy devmem bridge: %d\n", rc);
    }

    free(ctx);
}
