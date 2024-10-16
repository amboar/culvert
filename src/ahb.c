// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "ahb.h"
#include "log.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define AHB_CHUNK (1 << 20)

ssize_t ahb_siphon_out(struct ahb *ctx, uint32_t phys, ssize_t len, int outfd)
{
    ssize_t ingress, egress;
    void *chunk, *cursor;
    int rc = 0;

    if (!len)
        return 0;

    chunk = malloc(AHB_CHUNK);
    if (!chunk)
        return -errno;

    do {
        ingress = (len > AHB_CHUNK || len == -1) ? AHB_CHUNK : len;

        ingress = ahb_read(ctx, phys, chunk, ingress);
        if (ingress < 0) { rc = -EIO; goto done; }

        phys += ingress;
        if (len > 0) {
            len -= ingress;
        }

        cursor = chunk;
        while (ingress) {
            egress = write(outfd, cursor, ingress);
            if (egress == -1) { rc = -errno; goto done; }

            cursor += egress;
            ingress -= egress;
        }

        fprintf(stderr, ".");
    } while (!!len);

done:
    fprintf(stderr, "\n");
    free(chunk);

    return rc;
}

ssize_t ahb_siphon_in(struct ahb *ctx, uint32_t phys, ssize_t len, int infd)
{
    ssize_t ingress, egress;
    void *chunk;
    int rc = 0;

    chunk = malloc(AHB_CHUNK);
    if (!chunk)
        return -1;

    while ((ingress = read(infd, chunk, (len > AHB_CHUNK || len == -1) ? AHB_CHUNK : len))) {
        if (ingress < 0) { rc = -errno; goto done; }

        egress = ahb_write(ctx, phys, chunk, ingress);
        if (egress < 0) { rc = -EIO; goto done; }

        phys += ingress;
        if (len > 0) {
            len -= ingress;
        }

        fprintf(stderr, ".");
    }

done:
    fprintf(stderr, "\n");
    free(chunk);

    return rc;
}

int ahb_release_bridge(struct ahb *ctx)
{
    return ctx->drv->release ? ctx->drv->release(ctx) : 0;
}

int ahb_reinit_bridge(struct ahb *ctx)
{
    return ctx->drv->reinit ? ctx->drv->reinit(ctx) : 0;
}
