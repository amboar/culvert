// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.
// Copyright (C) 2021, Oracle and/or its affiliates.

#include "bridge.h"
#include "bridge/ilpc.h"
#include "compiler.h"
#include "log.h"
#include "rev.h"

#include "ccan/container_of/container_of.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define LPC_HICRB_ILPCB_RO (1 << 6)

#define to_ilpcb(ahb) container_of(ahb, struct ilpcb, ahb)

int ilpcb_probe(struct ilpcb *ctx)
{
    int rc;

    logd("Probing %s\n", ctx->ahb.drv->name);

    if (!sio_probe(&ctx->sio))
        return 0;

    rc = rev_probe(ilpcb_as_ahb(ctx));

    return rc < 0 ? rc : 1;
}

int ilpcb_mode(struct ilpcb *ctx)
{
    uint32_t hicrb = 0;
    int rc;

    rc = ilpcb_readl(ilpcb_as_ahb(ctx), 0x1e789100, &hicrb);
    if (rc)
        return rc;

    return !!(hicrb & LPC_HICRB_ILPCB_RO); /* Maps to enum ilpcb_mode */
}

ssize_t ilpcb_read(struct ahb *ahb, uint32_t addr, void *buf, size_t len)
{
    struct ilpcb *ctx = to_ilpcb(ahb);
    struct sio *sio = &ctx->sio;
    size_t remaining;
    uint8_t data;
    int locked;
    ssize_t rc;

    if (len > SSIZE_MAX)
        return -EINVAL;

    rc = sio_unlock(sio);
    if (rc)
        goto done;

    /* Select iLPC2AHB */
    rc = sio_select(sio, sio_ilpc);
    if (rc)
        goto done;

    /* Enable iLPC2AHB */
    rc = sio_writeb(sio, 0x30, 0x01);
    if (rc)
        goto done;

    /* 1-byte access */
    rc = sio_writeb(sio, 0xf8, 0);
    if (rc)
        goto done;

    /* XXX: Think about optimising this */
    remaining = len;
    while (remaining) {
        /* Address */
        rc |= sio_writeb(sio, 0xf0, addr >> 24);
        rc |= sio_writeb(sio, 0xf1, addr >> 16);
        rc |= sio_writeb(sio, 0xf2, addr >>  8);
        rc |= sio_writeb(sio, 0xf3, addr      );
        if (rc)
            goto done;

        /* Trigger */
        rc = sio_readb(sio, 0xfe, &data);
        if (rc)
            goto done;

        rc = sio_readb(sio, 0xf7, (uint8_t *)buf);
        if (rc)
            goto done;

        buf++;
        addr++;
        remaining--;
    }

done:
    locked = sio_lock(sio);
    if (locked) {
        errno = -locked;
        perror("sio_lock");
    }

    return rc ? rc : (ssize_t)len;
}

ssize_t ilpcb_write(struct ahb *ahb, uint32_t addr, const void *buf, size_t len)
{
    struct ilpcb *ctx = to_ilpcb(ahb);
    struct sio *sio = &ctx->sio;
    size_t remaining;
    int locked;
    ssize_t rc;

    if (len > SSIZE_MAX)
        return -EINVAL;

    rc = sio_unlock(sio);
    if (rc)
        goto done;

    /* Select iLPC2AHB */
    rc = sio_select(sio, sio_ilpc);
    if (rc)
        goto done;

    /* Enable iLPC2AHB */
    rc = sio_writeb(sio, 0x30, 0x01);
    if (rc)
        goto done;

    /* 1-byte access */
    rc = sio_writeb(sio, 0xf8, 0);
    if (rc)
        goto done;

    /* XXX: Think about optimising this */
    remaining = len;
    while (remaining) {
        /* Address */
        rc |= sio_writeb(sio, 0xf0, addr >> 24);
        rc |= sio_writeb(sio, 0xf1, addr >> 16);
        rc |= sio_writeb(sio, 0xf2, addr >>  8);
        rc |= sio_writeb(sio, 0xf3, addr      );
        if (rc)
            goto done;

        rc = sio_writeb(sio, 0xf7, *(uint8_t *)buf);
        if (rc)
            goto done;

        /* Trigger */
        rc = sio_writeb(sio, 0xfe, 0xcf);
        if (rc)
            goto done;

        buf++;
        remaining--;
    }

done:
    locked = sio_lock(sio);
    if (locked) {
        errno = -locked;
        perror("sio_lock");
    }

    return rc ? rc : (ssize_t)len;
}

/* Little-endian */
int ilpcb_readl(struct ahb *ahb, uint32_t addr, uint32_t *val)
{
    struct ilpcb *ctx = to_ilpcb(ahb);
    struct sio *sio = &ctx->sio;
    uint32_t extracted;
    uint8_t data;
    int locked;
    int rc;

    rc = sio_unlock(sio);
    if (rc)
        goto done;

    /* Select iLPC2AHB */
    rc = sio_select(sio, sio_ilpc);
    if (rc)
        goto done;

    /* Enable iLPC2AHB */
    rc = sio_writeb(sio, 0x30, 0x01);
    if (rc)
        goto done;

    /* 4-byte access */
    rc = sio_writeb(sio, 0xf8, 2);
    if (rc)
        goto done;

    /* Address */
    rc |= sio_writeb(sio, 0xf0, addr >> 24);
    rc |= sio_writeb(sio, 0xf1, addr >> 16);
    rc |= sio_writeb(sio, 0xf2, addr >>  8);
    rc |= sio_writeb(sio, 0xf3, addr      );
    if (rc)
        goto done;

    /* Trigger */
    rc = sio_readb(sio, 0xfe, &data);
    if (rc)
        goto done;

    /* Value */
    extracted = 0;
    rc |= sio_readb(sio, 0xf4, &data);
    extracted = (extracted << 8) | data;
    rc |= sio_readb(sio, 0xf5, &data);
    extracted = (extracted << 8) | data;
    rc |= sio_readb(sio, 0xf6, &data);
    extracted = (extracted << 8) | data;
    rc |= sio_readb(sio, 0xf7, &data);
    extracted = (extracted << 8) | data;
    if (rc)
        goto done;

    *val = extracted;

done:
    locked = sio_lock(sio);
    if (locked) {
        errno = -locked;
        perror("sio_lock");
    }

    return rc;
}

/* Little-endian */
int ilpcb_writel(struct ahb *ahb, uint32_t addr, uint32_t val)
{
    struct ilpcb *ctx = to_ilpcb(ahb);
    struct sio *sio = &ctx->sio;
    int locked;
    int rc;

    rc = sio_unlock(sio);
    if (rc)
        goto done;

    /* Select iLPC2AHB */
    rc = sio_select(sio, sio_ilpc);
    if (rc)
        goto done;

    /* Enable iLPC2AHB */
    rc = sio_writeb(sio, 0x30, 0x01);
    if (rc)
        goto done;

    /* 4-byte access */
    rc = sio_writeb(sio, 0xf8, 2);
    if (rc)
        goto done;

    /* Address */
    rc |= sio_writeb(sio, 0xf0, addr >> 24);
    rc |= sio_writeb(sio, 0xf1, addr >> 16);
    rc |= sio_writeb(sio, 0xf2, addr >>  8);
    rc |= sio_writeb(sio, 0xf3, addr >>  0);
    if (rc)
        goto done;

    /* Value */
    rc |= sio_writeb(sio, 0xf4, val >> 24);
    rc |= sio_writeb(sio, 0xf5, val >> 16);
    rc |= sio_writeb(sio, 0xf6, val >>  8);
    rc |= sio_writeb(sio, 0xf7, val >>  0);
    if (rc)
        goto done;

    /* Trigger */
    rc = sio_writeb(sio, 0xfe, 0xcf);
    if (rc)
        goto done;

done:
    locked = sio_lock(sio);
    if (locked) {
        errno = -locked;
        perror("Failed to lock SuperIO device");
    }

    return rc;
}

static const struct ahb_ops ilpcb_ops = {
    .read = ilpcb_read,
    .write = ilpcb_write,
    .readl = ilpcb_readl,
    .writel = ilpcb_writel
};

static struct ahb *ilpcb_driver_probe(int argc, char *argv[]);
static void ilpcb_driver_destroy(struct ahb *ahb);

static struct bridge_driver ilpcb_driver = {
    .name = "ilpc",
    .probe = ilpcb_driver_probe,
    .destroy = ilpcb_driver_destroy,
};
REGISTER_BRIDGE_DRIVER(ilpcb_driver);

int ilpcb_init(struct ilpcb *ctx)
{
    ahb_init_ops(&ctx->ahb, &ilpcb_driver, &ilpcb_ops);

    return sio_init(&ctx->sio);
}

int ilpcb_destroy(struct ilpcb *ctx)
{
    return sio_destroy(&ctx->sio);
}

static struct ahb *
ilpcb_driver_probe(int argc, char *argv[] __unused)
{
    struct ilpcb *ctx;
    int rc;

    // This driver doesn't require args, so if there are any we're not trying to probe it
    if (argc > 0) {
        return NULL;
    }

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return NULL;
    }

    if ((rc = ilpcb_init(ctx)) < 0) {
        logd("Failed to initialise iLPC bridge: %d\n", rc);
        goto cleanup_ctx;
    }

    if ((rc = ilpcb_probe(ctx)) < 0) {
        logd("Failed iLPC probe: %d\n", rc);
        goto destroy_ctx;
    }

    return ilpcb_as_ahb(ctx);

destroy_ctx:
    ilpcb_destroy(ctx);

cleanup_ctx:
    free(ctx);

    return NULL;
}

static void ilpcb_driver_destroy(struct ahb *ahb)
{
    struct ilpcb *ctx = to_ilpcb(ahb);
    int rc;

    if ((rc = ilpcb_destroy(ctx)) < 0) {
        loge("Failed to destroy iLPC bridge: %d\n", rc);
    }

    free(ctx);
}
