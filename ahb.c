// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "ahb.h"
#include "debug.h"
#include "devmem.h"
#include "ilpc.h"
#include "l2a.h"
#include "log.h"
#include "p2a.h"
#include "priv.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

const char *ahb_interface_names[ahb_max_interfaces] = {
    [ahb_ilpcb] = "iLPC2AHB",
    [ahb_l2ab] = "LPC2AHB",
    [ahb_p2ab] = "P2A",
    [ahb_debug] = "Debug UART",
    [ahb_devmem] = "devmem",
};

static void ahb_notify_bridge(struct ahb *ctx)
{
    logi("Initialised %s AHB interface\n", ahb_interface_names[ctx->bridge]);
}

void ahb_use(struct ahb *ctx, enum ahb_bridge type, void *bridge)
{
    ctx->bridge = type;
    if (type == ahb_ilpcb)
        ctx->ilpcb = bridge;
    else if (type == ahb_l2ab)
        ctx->l2ab = bridge;
    else if (type == ahb_p2ab)
        ctx->p2ab = bridge;
    else if (type == ahb_debug)
        ctx->debug = bridge;
    else if (type == ahb_devmem)
        ctx->devmem = bridge;
    else
        assert(false);

    ahb_notify_bridge(ctx);
}

int ahb_init(struct ahb *ctx, enum ahb_bridge type, ...)
{
    int rc;

    if (type == ahb_ilpcb) {
        ctx->ilpcb = malloc(sizeof(*ctx->ilpcb));
        rc = ilpcb_init(ctx->ilpcb);
        if (rc < 0) {
            free(ctx->ilpcb);
            return rc;
        }
    } else if (type == ahb_l2ab) {
        ctx->l2ab = malloc(sizeof(*ctx->l2ab));
        rc = l2ab_init(ctx->l2ab);
        if (rc < 0) {
            free(ctx->l2ab);
            return rc;
        }
    } else if (type == ahb_p2ab) {
        ctx->p2ab = malloc(sizeof(*ctx->p2ab));
        rc = p2ab_init(ctx->p2ab, AST_PCI_VID, AST_PCI_DID_VGA);
        if (rc < 0) {
            free(ctx->p2ab);
            return rc;
        }
    } else if (type == ahb_debug) {
        const char *interface, *ip, *username, *password;
        va_list args;
        int port;

        ctx->debug = malloc(sizeof(*ctx->debug));

        va_start(args, type);
        interface = va_arg(args, const char *);
        ip = va_arg(args, const char *);
        port = va_arg(args, int);
        username = va_arg(args, const char *);
        password = va_arg(args, const char *);
        va_end(args);
        rc = debug_init(ctx->debug, interface, ip, port, username, password);
        if (rc < 0) {
            free(ctx->debug);
            return rc;
        }
        rc = debug_enter(ctx->debug);
        if (rc < 0) {
            debug_destroy(ctx->debug);
            free(ctx->debug);
            return rc;
        }
    } else if (type == ahb_devmem) {
        ctx->devmem = malloc(sizeof(*ctx->devmem));
        rc = devmem_init(ctx->devmem);
        if (rc < 0) {
            free(ctx->devmem);
            return rc;
        }
    } else
        return -EINVAL;

    ctx->bridge = type;

    ahb_notify_bridge(ctx);

    return rc;
}

int ahb_cleanup(struct ahb *ctx)
{
    if (ctx->bridge == ahb_ilpcb || ctx->bridge == ahb_l2ab ||
            ctx->bridge == ahb_p2ab || ctx->bridge == ahb_devmem) {
        ahb_destroy(ctx);
    } else if (ctx->bridge == ahb_debug) {
        debug_cleanup(ctx->debug);
        debug_destroy(ctx->debug);
    } else
        assert(false);

    return 0;
}

int ahb_destroy(struct ahb *ctx)
{
    if (ctx->bridge == ahb_ilpcb) {
        ilpcb_destroy(ctx->ilpcb);
        free(ctx->ilpcb);
    } else if (ctx->bridge == ahb_l2ab) {
        l2ab_destroy(ctx->l2ab);
        free(ctx->l2ab);
    } else if (ctx->bridge == ahb_p2ab) {
        p2ab_destroy(ctx->p2ab);
        free(ctx->p2ab);
    } else if (ctx->bridge == ahb_debug) {
        debug_exit(ctx->debug);
        debug_destroy(ctx->debug);
        free(ctx->debug);
    } else if (ctx->bridge == ahb_devmem) {
        devmem_destroy(ctx->devmem);
        free(ctx->devmem);
    } else
        assert(false);

    return 0;
}

ssize_t ahb_read(struct ahb *ctx, uint32_t phys, void *buf, size_t len)
{
    if (ctx->bridge == ahb_ilpcb)
        return ilpcb_read(ctx->ilpcb, phys, buf, len);
    else if (ctx->bridge == ahb_l2ab)
        return l2ab_read(ctx->l2ab, phys, buf, len);
    else if (ctx->bridge == ahb_p2ab)
        return p2ab_read(ctx->p2ab, phys, buf, len);
    else if (ctx->bridge == ahb_debug)
        return debug_read(ctx->debug, phys, buf, len);
    else if (ctx->bridge == ahb_devmem)
        return devmem_read(ctx->devmem, phys, buf, len);

    return -ENOTSUP;
}

ssize_t ahb_write(struct ahb *ctx, uint32_t phys, const void *buf, size_t len)
{
    if (ctx->bridge == ahb_ilpcb)
        return ilpcb_write(ctx->ilpcb, phys, buf, len);
    else if (ctx->bridge == ahb_l2ab)
        return l2ab_write(ctx->l2ab, phys, buf, len);
    else if (ctx->bridge == ahb_p2ab)
        return p2ab_write(ctx->p2ab, phys, buf, len);
    else if (ctx->bridge == ahb_debug)
        return debug_write(ctx->debug, phys, buf, len);
    else if (ctx->bridge == ahb_devmem)
        return devmem_write(ctx->devmem, phys, buf, len);

    return -ENOTSUP;
}

int ahb_readl(struct ahb *ctx, uint32_t phys, uint32_t *val)
{
    if (ctx->bridge == ahb_ilpcb)
        return ilpcb_readl(ctx->ilpcb, phys, val);
    else if (ctx->bridge == ahb_l2ab)
        return l2ab_readl(ctx->l2ab, phys, val);
    else if (ctx->bridge == ahb_p2ab)
        return p2ab_readl(ctx->p2ab, phys, val);
    else if (ctx->bridge == ahb_debug)
        return debug_readl(ctx->debug, phys, val);
    else if (ctx->bridge == ahb_devmem)
        return devmem_readl(ctx->devmem, phys, val);

    return -ENOTSUP;
}

int ahb_writel(struct ahb *ctx, uint32_t phys, uint32_t val)
{
    if (ctx->bridge == ahb_ilpcb)
        return ilpcb_writel(ctx->ilpcb, phys, val);
    else if (ctx->bridge == ahb_l2ab)
        return l2ab_writel(ctx->l2ab, phys, val);
    else if (ctx->bridge == ahb_p2ab)
        return p2ab_writel(ctx->p2ab, phys, val);
    else if (ctx->bridge == ahb_debug)
        return debug_writel(ctx->debug, phys, val);
    else if (ctx->bridge == ahb_devmem)
        return devmem_writel(ctx->devmem, phys, val);

    return -ENOTSUP;
}

#define AHB_CHUNK (1 << 20)

ssize_t ahb_siphon_in(struct ahb *ctx, uint32_t phys, size_t len, int outfd)
{
    ssize_t ingress, egress, remaining;
    void *chunk, *cursor;
    int rc = 0;

    chunk = malloc(AHB_CHUNK);
    if (!chunk)
        return -errno;

    remaining = len;
    do {
        ingress = remaining > AHB_CHUNK ? AHB_CHUNK : remaining;

        ingress = ahb_read(ctx, phys, chunk, ingress);
        if (ingress < 0) { rc = ingress; goto done; }

        phys += ingress;
        remaining -= ingress;

        cursor = chunk;
        while (ingress) {
            egress = write(outfd, cursor, ingress);
            if (egress == -1) { rc = -errno; goto done; }

            cursor += egress;
            ingress -= egress;
        }

        fprintf(stderr, ".");
    } while (remaining);

done:
    fprintf(stderr, "\n");
    free(chunk);

    return rc;
}

ssize_t ahb_siphon_out(struct ahb *ctx, uint32_t phys, int infd)
{
    ssize_t ingress, egress;
    void *chunk;
    int rc = 0;

    chunk = malloc(AHB_CHUNK);
    if (!chunk)
        return -errno;

    while ((ingress = read(infd, chunk, AHB_CHUNK))) {
        if (ingress < 0) { rc = -errno; goto done; }

        egress = ahb_write(ctx, phys, chunk, ingress);
        if (egress < 0) { rc = egress; goto done; }

        phys += ingress;

        fprintf(stderr, ".");
    }

done:
    fprintf(stderr, "\n");
    free(chunk);

    return rc;
}
