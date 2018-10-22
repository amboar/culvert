// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "sio.h"

int sio_init(struct sio *ctx)
{
    return lpc_init(&ctx->io, "io");
}

int sio_destroy(struct sio *ctx)
{
    return lpc_destroy(&ctx->io);
}

int sio_lock(struct sio *ctx)
{
    return lpc_writeb(&ctx->io, 0x2e, 0xaa);
}

int sio_unlock(struct sio *ctx)
{
    int rc;

    rc  = lpc_writeb(&ctx->io, 0x2e, 0xa5);
    rc |= lpc_writeb(&ctx->io, 0x2e, 0xa5);

    return rc;
}

int sio_select(struct sio *ctx, enum sio_dev dev)
{
    return sio_writeb(ctx, 0x07, dev);
}

int sio_present(struct sio *ctx)
{
    uint8_t dev;
    int rc;

    /* Dumb heuristics as we don't have access to the LPCHC */
    rc = sio_unlock(ctx);
    if (rc)
        goto out;

    rc = sio_select(ctx, sio_suart1);
    if (rc)
        goto out;

    rc = sio_readb(ctx, 0x07, &dev);
    if (rc)
        goto out;

    rc = (dev == sio_suart1);
    if (!rc)
        goto out;

    rc = sio_select(ctx, sio_suart4);
    if (rc)
        goto out;

    rc = sio_readb(ctx, 0x07, &dev);
    if (rc)
        goto out;

    rc = (dev == sio_suart4);

out:
    sio_lock(ctx);

    return rc;
}

int sio_readb(struct sio *ctx, uint32_t addr, uint8_t *val)
{
    int rc;

    rc = lpc_writeb(&ctx->io, 0x2e, addr);
    if (rc)
        return rc;

    return lpc_readb(&ctx->io, 0x2f, val);
}

int sio_writeb(struct sio *ctx, uint32_t addr, uint8_t val)
{
    int rc;

    rc = lpc_writeb(&ctx->io, 0x2e, addr);
    if (rc)
        return rc;

    return lpc_writeb(&ctx->io, 0x2f, val);
}
