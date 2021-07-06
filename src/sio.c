// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "log.h"
#include "sio.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>

#define SIO_ADDR(ctx) ((ctx)->base)
#define SIO_DATA(ctx) ((ctx)->base + 1)

int sio_init(struct sio *ctx)
{
    ctx->base = 0x2e;
    return lpc_init(&ctx->io, "io");
}

int sio_destroy(struct sio *ctx)
{
    return lpc_destroy(&ctx->io);
}

int sio_lock(struct sio *ctx)
{
    return lpc_writeb(&ctx->io, SIO_ADDR(ctx), 0xaa);
}

int sio_unlock(struct sio *ctx)
{
    int rc;

    rc  = lpc_writeb(&ctx->io, SIO_ADDR(ctx), 0xa5);
    rc |= lpc_writeb(&ctx->io, SIO_ADDR(ctx), 0xa5);

    return rc;
}

int sio_select(struct sio *ctx, enum sio_dev dev)
{
    return sio_writeb(ctx, 0x07, dev);
}

static int sio_present(struct sio *ctx)
{
    uint8_t dev;
    int rc;

    logd("Probing 0x%x for SuperIO\n", ctx->base);

    /* Dumb heuristics as we don't have access to the LPCHC */
    rc = sio_unlock(ctx);
    logt("Unlocking SuperIO: %d\n", rc);
    if (rc)
        goto out;

    rc = sio_select(ctx, sio_suart1);
    logt("Selecting SuperIO device %d (SUART1): %d\n", sio_suart1, rc);
    if (rc)
        goto out;

    rc = sio_readb(ctx, 0x07, &dev);
    logt("Found device %d selected: %d\n", dev, rc);
    if (rc)
        goto out;

    rc = (dev == sio_suart1);
    if (!rc)
        goto out;

    rc = sio_select(ctx, sio_suart4);
    logt("Selecting SuperIO device %d (SUART4): %d\n", sio_suart4, rc);
    if (rc)
        goto out;

    rc = sio_readb(ctx, 0x07, &dev);
    logt("Found device %d selected: %d\n", dev, rc);
    if (rc)
        goto out;

    rc = (dev == sio_suart4);

out:
    sio_lock(ctx);
    logt("Locking SuperIO\n");

    return rc;
}

int sio_probe(struct sio *ctx)
{
    bool found;

    ctx->base = 0x2e;
    found = sio_present(ctx);
    if (!found) {
        ctx->base = 0x4e;
        found = sio_present(ctx);
    }

    if (found) {
        logd("Found SuperIO device at 0x%" PRIx16 "\n", ctx->base);
    } else {
        logd("SuperIO disabled\n");
    }

    return found;
}

int sio_readb(struct sio *ctx, uint32_t addr, uint8_t *val)
{
    int rc;

    rc = lpc_writeb(&ctx->io, SIO_ADDR(ctx), addr);
    if (rc)
        return rc;

    return lpc_readb(&ctx->io, SIO_DATA(ctx), val);
}

int sio_writeb(struct sio *ctx, uint32_t addr, uint8_t val)
{
    int rc;

    rc = lpc_writeb(&ctx->io, SIO_ADDR(ctx), addr);
    if (rc)
        return rc;

    return lpc_writeb(&ctx->io, SIO_DATA(ctx), val);
}
