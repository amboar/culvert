// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.
// Copyright (C) 2021, Oracle and/or its affiliates.

#include "ahb.h"
#include "bridge.h"
#include "compiler.h"
#include "log.h"
#include "mb.h"
#include "mmio.h"
#include "p2a.h"
#include "pci.h"
#include "rev.h"

#include "ccan/container_of/container_of.h"

#include <assert.h>
#include <endian.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define AST_MMIO_BAR            1
#define AST_MMIO_LEN            (128 * 1024)
#define P2AB_PKR                0xf000
#define P2AB_RBAR               0xf004
#define   P2AB_RBAR_REMAP_MASK  0xffff0000
#define P2AB_WINDOW_BASE        0x10000
#define P2AB_WINDOW_LEN         0x10000

#define to_p2ab(ahb) container_of(ahb, struct p2ab, ahb)

static int __p2ab_readl(struct p2ab *ctx, size_t addr, uint32_t *val)
{
    assert(addr < (AST_MMIO_LEN - sizeof(val) + 1));
    assert(!(addr & (sizeof(val) - 1)));

    *val = le32toh(*((volatile uint32_t *)(ctx->mmio + addr)));

    iob();

    return 0;
}

static int __p2ab_writel(struct p2ab *ctx, size_t addr, uint32_t val)
{
    assert(addr < (AST_MMIO_LEN - sizeof(val) + 1));
    assert(!(addr & (sizeof(val) - 1)));

    *((uint32_t *)(ctx->mmio + addr)) = htole32(val);

    iob();

    return 0;
}

static inline int p2ab_unlock(struct p2ab *ctx)
{
    int rc;

    rc = __p2ab_writel(ctx, P2AB_PKR, 1);

    return rc;
}

static inline int p2ab_lock(struct p2ab *ctx)
{
    int rc;

    rc = __p2ab_writel(ctx, P2AB_PKR, 0);

    return rc;
}

int p2ab_probe(struct p2ab *ctx)
{
    int64_t rc;

    logd("Probing %s\n", ctx->ahb.drv->name);

    rc = rev_probe(p2ab_as_ahb(ctx));
    if (rc < 0)
        return rc;

    return rc < 0 ? rc : 1;
}

int64_t p2ab_map(struct p2ab *ctx, uint32_t phys, size_t len __unused)
{
    uint32_t rbar;
    uint32_t offset;
    int64_t rc;

    rbar = phys & P2AB_RBAR_REMAP_MASK;
    offset = phys & ~P2AB_RBAR_REMAP_MASK;

    if (ctx->rbar == rbar)
        return offset;

    rc = __p2ab_writel(ctx, P2AB_RBAR, rbar);

    if (rc < 0)
        return rc;

    ctx->rbar = rbar;

    return offset;
}

ssize_t p2ab_read(struct ahb *ahb, uint32_t phys, void *buf, size_t len)
{
    struct p2ab *ctx = to_p2ab(ahb);
    size_t remaining = len;
    size_t ingress;
    int64_t rc;

    do {
        ingress = remaining > P2AB_WINDOW_LEN ? P2AB_WINDOW_LEN : remaining;

        rc = p2ab_map(ctx, phys, ingress);
        if (rc < 0)
            return rc;

        mmio_memcpy(buf, (ctx->mmio + P2AB_WINDOW_BASE + rc), ingress);
        phys += ingress;
        buf += ingress;
        remaining -= ingress;
    } while (remaining);

    return len;
}

ssize_t p2ab_write(struct ahb *ahb, uint32_t phys, const void *buf, size_t len)
{
    struct p2ab *ctx = to_p2ab(ahb);
    size_t remaining = len;
    size_t egress;
    int64_t rc;

    do {
        egress = remaining > P2AB_WINDOW_LEN ? P2AB_WINDOW_LEN : remaining;

        rc = p2ab_map(ctx, phys, egress);
        if (rc < 0)
            return rc;

        mmio_memcpy((ctx->mmio + P2AB_WINDOW_BASE + rc), buf, egress);
        phys += egress;
        buf += egress;
        remaining -= egress;
    } while (remaining);

    return len;
}

int p2ab_readl(struct ahb *ahb, uint32_t phys, uint32_t *val)
{
    struct p2ab *ctx = to_p2ab(ahb);
    uint32_t le;
    ssize_t rc;

    if (phys & 0x3)
        return -EINVAL;

    rc = p2ab_map(ctx, phys, sizeof(*val));
    if (rc < 0)
        return rc;

    le = *(uint32_t *)(ctx->mmio + P2AB_WINDOW_BASE + rc);

    *val = le32toh(le);

    return 0;
}

int p2ab_writel(struct ahb *ahb, uint32_t phys, uint32_t val)
{
    struct p2ab *ctx = to_p2ab(ahb);
    int rc;

    val = htole32(val);

    rc = p2ab_map(ctx, phys, sizeof(val));
    if (rc < 0)
        return rc;

    *(uint32_t *)(ctx->mmio + P2AB_WINDOW_BASE + rc) = val;

    return 0;
}

static const struct ahb_ops p2ab_ahb_ops = {
    .read = p2ab_read,
    .write = p2ab_write,
    .readl = p2ab_readl,
    .writel = p2ab_writel,
};

static struct ahb *p2ab_driver_probe(int argc, char *argv[]);
static int p2ab_driver_reinit(struct ahb *ahb);
static void p2ab_driver_destroy(struct ahb *ahb);

static const struct bridge_driver p2ab_driver = {
    .name = "p2a",
    .probe = p2ab_driver_probe,
    .reinit = p2ab_driver_reinit,
    .destroy = p2ab_driver_destroy,
};
REGISTER_BRIDGE_DRIVER(p2ab_driver);

int p2ab_init(struct p2ab *ctx, uint16_t vid, uint16_t did)
{
    int rc;

    rc = pci_open(vid, did, AST_MMIO_BAR);
    if (rc < 0)
        return rc;

    ctx->res = rc;
    ctx->mmio = mmap(0, AST_MMIO_LEN, PROT_READ | PROT_WRITE, MAP_SHARED,
                     ctx->res, 0);
    if (ctx->mmio == MAP_FAILED) {
        rc = -errno;
        goto cleanup_pci;
    }

    /* ensure the HW and SW rbar values are in sync */
    ctx->rbar = 0;
    __p2ab_writel(ctx, P2AB_RBAR, ctx->rbar);

    if ((rc = p2ab_unlock(ctx)) < 0)
        goto cleanup_mmap;

    ahb_init_ops(&ctx->ahb, &p2ab_driver, &p2ab_ahb_ops);

    return 0;

cleanup_mmap:
    munmap(ctx->mmio, AST_MMIO_LEN);

cleanup_pci:
    pci_close(ctx->res);

    return rc;
}

int p2ab_destroy(struct p2ab *ctx)
{
    int rc;

    rc = p2ab_lock(ctx);
    if (rc < 0)
        return rc;

    rc = munmap(ctx->mmio, AST_MMIO_LEN);
    if (rc == -1)
        return -errno;

    return pci_close(ctx->res);
}

static struct ahb *
p2ab_driver_probe(int argc, char *argv[] __unused)
{
    struct p2ab *ctx;
    int rc;

    // This driver doesn't require args, so if there are any we're not trying to probe it
    if (argc > 0) {
        return NULL;
    }

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return NULL;
    }

    if ((rc = p2ab_init(ctx, AST_PCI_VID, AST_PCI_DID_VGA)) < 0) {
        logd("Failed to initialise P2A bridge: %d\n", rc);
        goto cleanup_ctx;
    }

    if ((rc = p2ab_probe(ctx)) < 0) {
        logd("Failed P2A probe: %d\n", rc);
        goto destroy_ctx;
    }

    return p2ab_as_ahb(ctx);

destroy_ctx:
    p2ab_destroy(ctx);

cleanup_ctx:
    free(ctx);

    return NULL;
}

static int p2ab_driver_reinit(struct ahb *ahb)
{
    struct p2ab *ctx = to_p2ab(ahb);

    /* Update the software cache with the hardware state */
    return __p2ab_readl(ctx, P2AB_RBAR, &ctx->rbar);
}

static void p2ab_driver_destroy(struct ahb *ahb)
{
    struct p2ab *ctx = to_p2ab(ahb);
    int rc;

    if ((rc = p2ab_destroy(ctx)) < 0) {
        loge("Failed to destroy P2A bridge: %d\n", rc);
    }

    free(ctx);
}
