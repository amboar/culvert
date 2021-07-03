// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "l2a.h"

#define LPC_HICR7               0x1e789088
#define LPC_HICR8               0x1e78908c
#define L2AB_WINDOW_SIZE        (1 << 27)

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

    rc = ilpcb_readl(ilpcb, LPC_HICR7, &ctx->restore7);
    if (rc)
        goto cleanup;

    rc = ilpcb_readl(ilpcb, LPC_HICR8, &ctx->restore8);
    if (rc)
        goto cleanup;

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

    rc = ilpcb_writel(ilpcb, 0x1e78908c, ctx->restore8);
    if (rc)
        return rc;

    rc = ilpcb_writel(ilpcb, 0x1e789088, ctx->restore7);
    if (rc)
        return rc;

    rc = ilpcb_destroy(&ctx->ilpcb);
    if (rc)
        return rc;

    return lpc_destroy(&ctx->fw);
}

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

    rc = ilpcb_writel(ilpcb, LPC_HICR7, hicr7);
    if (rc)
        return rc;

    rc = ilpcb_writel(ilpcb, LPC_HICR8, hicr8);
    if (rc)
        return rc;

    ctx->phys = hicr7; /* This is correct as we're mapping to 0 in LPC FW */
    ctx->len = len;

    return phys & 0xffff;
}

ssize_t l2ab_read(struct l2ab *ctx, uint32_t phys, void *buf, size_t len)
{
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

ssize_t l2ab_write(struct l2ab *ctx, uint32_t phys, const void *buf, size_t len)
{
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

int l2ab_readl(struct l2ab *ctx, uint32_t phys, uint32_t *val)
{
    return ilpcb_readl(&ctx->ilpcb, phys, val);
}

int l2ab_writel(struct l2ab *ctx, uint32_t phys, uint32_t val)
{
    return ilpcb_writel(&ctx->ilpcb, phys, val);
}
