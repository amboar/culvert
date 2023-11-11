// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "bridge.h"
#include "compiler.h"
#include "l2a.h"
#include "log.h"

#include "ccan/container_of/container_of.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define LPC_HICR7               0x1e789088
#define LPC_HICR8               0x1e78908c
#define L2AB_WINDOW_SIZE        (1 << 27)

#define to_l2ab(ahb) container_of(ahb, struct l2ab, ahb)

/* @return The LPC FW offset mapped to phys */
int64_t l2ab_map(struct l2ab *ctx, uint32_t phys, size_t len)
{
    struct ilpcb *ilpcb = &ctx->ilpcb;
    uint32_t aligned = phys & ~0xffff;
    uint32_t hicr7, hicr8;
    int rc;

    /* Check if the requested phys/len fit inside the current mapping */
    if (phys >= ctx->phys && (phys + len) <= (ctx->phys + ctx->len))
        return phys - ctx->phys;

    /* Check if we'd intersect hiomapd/skiboot territory */
    if (len > L2AB_WINDOW_SIZE)
        return -EINVAL;

    /* Expand length to account for alignment */
    if (len < (phys + len - aligned))
        len = phys + len - aligned;

    /*
     * Open a window sized to the nearest power of 2 greater than len that is
     * at least 2^16 bytes, due to the structure of HICR8.
     */
    if (len < (1 << 16))
        len = (1 << 16);

    if (__builtin_popcount(len) != 1)
        len = 1 << (31 - __builtin_clz(len));

    hicr7 = (phys & ~(0xffff));
    hicr8 = (~(len - 1)) | ((len - 1) >> 16);

    rc = ilpcb_writel(ilpcb_as_ahb(ilpcb), LPC_HICR7, hicr7);
    if (rc)
        return rc;

    rc = ilpcb_writel(ilpcb_as_ahb(ilpcb), LPC_HICR8, hicr8);
    if (rc)
        return rc;

    ctx->phys = hicr7; /* This is correct as we're mapping to 0 in LPC FW */
    ctx->len = len;

    return phys & 0xffff;
}

ssize_t l2ab_read(struct ahb *ahb, uint32_t phys, void *buf, size_t len)
{
    struct l2ab *ctx = to_l2ab(ahb);
    size_t remaining = len;
    ssize_t ingress;
    int64_t offset;

    do {
        ingress = remaining > L2AB_WINDOW_SIZE ? L2AB_WINDOW_SIZE : remaining;

        offset = l2ab_map(ctx, phys, ingress);
        if (offset < 0)
            return offset;

        ingress = lpc_read(&ctx->fw, offset, buf, ingress);
        if (ingress < 0)
            return ingress;

        phys += ingress;
        buf += ingress;
        remaining -= ingress;
    } while (remaining);

    return len;
}

ssize_t l2ab_write(struct ahb *ahb, uint32_t phys, const void *buf, size_t len)
{
    struct l2ab *ctx = to_l2ab(ahb);
    size_t remaining = len;
    ssize_t egress;
    int64_t offset;

    do {
        egress = remaining > L2AB_WINDOW_SIZE ? L2AB_WINDOW_SIZE : remaining;

        offset = l2ab_map(ctx, phys, egress);
        if (offset < 0)
            return offset;

        egress = lpc_write(&ctx->fw, offset, buf, egress);
        if (egress < 0)
            return egress;

        phys += egress;
        buf += egress;
        remaining -= egress;
    } while (remaining);

    return len;
}

int l2ab_readl(struct ahb *ahb, uint32_t phys, uint32_t *val)
{
    struct l2ab *ctx = to_l2ab(ahb);
    return ilpcb_readl(ilpcb_as_ahb(&ctx->ilpcb), phys, val);
}

int l2ab_writel(struct ahb *ahb, uint32_t phys, uint32_t val)
{
    struct l2ab *ctx = to_l2ab(ahb);
    return ilpcb_writel(ilpcb_as_ahb(&ctx->ilpcb), phys, val);
}

static const struct ahb_ops l2ab_ahb_ops = {
    .read = l2ab_read,
    .write = l2ab_write,
    .readl = l2ab_readl,
    .writel = l2ab_writel,
};

static struct ahb *l2ab_driver_probe(int argc, char *argv[]);
static void l2ab_driver_destroy(struct ahb *ahb);

static const struct bridge_driver l2ab_driver = {
    .name = "l2a",
    .probe = l2ab_driver_probe,
    .destroy = l2ab_driver_destroy,
};
REGISTER_BRIDGE_DRIVER(l2ab_driver);

int l2ab_init(struct l2ab *ctx)
{
    struct ilpcb *ilpcb = &ctx->ilpcb;
    int rc;

    rc = lpc_init(&ctx->fw, "fw");
    if (rc)
        return rc;

    rc = ilpcb_init(ilpcb);
    if (rc)
        return rc;

    rc = ilpcb_probe(ilpcb);
    if (rc)
        goto cleanup;

    rc = ilpcb_readl(ilpcb_as_ahb(ilpcb), LPC_HICR7, &ctx->restore7);
    if (rc)
        goto cleanup;

    rc = ilpcb_readl(ilpcb_as_ahb(ilpcb), LPC_HICR8, &ctx->restore8);
    if (rc)
        goto cleanup;

    ahb_init_ops(&ctx->ahb, &l2ab_driver, &l2ab_ahb_ops);

    return 0;

cleanup:
    ilpcb_destroy(ilpcb);
    lpc_destroy(&ctx->fw);

    return rc;
}

int l2ab_destroy(struct l2ab *ctx)
{
    struct ilpcb *ilpcb = &ctx->ilpcb;
    int rc;

    rc = ilpcb_writel(ilpcb_as_ahb(ilpcb), LPC_HICR8, ctx->restore8);
    if (rc)
        return rc;

    rc = ilpcb_writel(ilpcb_as_ahb(ilpcb), LPC_HICR7, ctx->restore7);
    if (rc)
        return rc;

    rc = ilpcb_destroy(&ctx->ilpcb);
    if (rc)
        return rc;

    return lpc_destroy(&ctx->fw);
}

static struct ahb *
l2ab_driver_probe(int argc, char *argv[] __unused)
{
    struct l2ab *ctx;
    int rc;

    // This driver doesn't require args, so if there are any we're not trying to probe it
    if (argc > 0) {
        return NULL;
    }

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return NULL;
    }

    if ((rc = l2ab_init(ctx)) < 0) {
        logd("Failed to initialise L2A bridge: %d\n", rc);
        goto cleanup_ctx;
    }

    return l2ab_as_ahb(ctx);

cleanup_ctx:
    free(ctx);

    return NULL;
}

static void l2ab_driver_destroy(struct ahb *ahb)
{
    struct l2ab *ctx = to_l2ab(ahb);
    int rc;

    if ((rc = l2ab_destroy(ctx)) < 0) {
        loge("Failed to destroy L2A bridge: %d\n", rc);
    }

    free(ctx);
}
