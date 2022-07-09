// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2022 IBM Corp.

#include "compiler.h"
#include "log.h"
#include "soc/ilpcctl.h"
#include "soc/sioctl.h"

#include "ccan/container_of/container_of.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#define LPC_HICRB               0x100
#define   LPC_HICRB_ILPC_RO     (1 << 6)

struct ilpcctl {
    struct bridgectl bridge;
    struct soc *soc;
    struct soc_region lpc;
    struct sioctl *sioctl;
};
#define to_ilpcctl(b) container_of((b), struct ilpcctl, bridge)

struct bridgectl *ilpcctl_as_bridgectl(struct ilpcctl *ctx)
{
    return &ctx->bridge;
}

static const char *ilpcctl_name(struct bridgectl *bridge __unused)
{
    return "ilpc";
}

static int ilpcctl_enforce(struct bridgectl *bridge, enum bridge_mode mode)
{
    struct ilpcctl *ctx = to_ilpcctl(bridge);
    uint32_t hicrb;
    int rc;

    if (mode == bm_disabled) {
        if ((rc = sioctl_decode_configure(ctx->sioctl, sioctl_decode_disable)) < 0) {
            loge("Failed to disable SuperIO decoding: %d\n");
        }

        return rc;
    }

    if ((rc = soc_readl(ctx->soc, ctx->lpc.start + LPC_HICRB, &hicrb)) < 0) {
        loge("Failed to read LPC HICRB: %d\n", rc);
        return rc;
    }

    if (mode == bm_restricted) {
        hicrb |= LPC_HICRB_ILPC_RO;
    } else {
        assert(mode == bm_permissive);
        hicrb &= ~LPC_HICRB_ILPC_RO;
    }

    if ((rc = soc_writel(ctx->soc, ctx->lpc.start + LPC_HICRB, hicrb)) < 0) {
        loge("Failed to write LPC HICRB: %d\n", rc);
        return rc;
    }

    /* FIXME: Sort out a way to configure which IO address we use */
    if ((rc = sioctl_decode_configure(ctx->sioctl, sioctl_decode_2e)) < 0) {
        loge("Failed to enable SuperIO decoding on 0x2e: %d\n", rc);
    }

    return rc;
}

static int ilpcctl_status(struct bridgectl *bridge, enum bridge_mode *mode)
{
    struct ilpcctl *ctx = to_ilpcctl(bridge);
    enum sioctl_decode decode;
    uint32_t hicrb;
    int rc;

    if ((rc = sioctl_decode_status(ctx->sioctl, &decode)) < 0) {
        loge("Failed to read SuperIO decode status: %d\n", rc);
        return rc;
    }

    if (decode == sioctl_decode_disable) {
        *mode = bm_disabled;
        return 0;
    }

    if ((rc = soc_readl(ctx->soc, ctx->lpc.start + LPC_HICRB, &hicrb)) < 0) {
        loge("Failed to read LPC HICRB: %d\n", rc);
        return rc;
    }

    *mode = (hicrb & LPC_HICRB_ILPC_RO) ? bm_restricted : bm_permissive;

    return 0;
}

static int ilpcctl_report(struct bridgectl *bridge, int fd, enum bridge_mode *mode)
{
    int rc;

    if ((rc = ilpcctl_status(bridge, mode)) < 0) {
        loge("Failed to read iLPC bridge status: %d\n", rc);
        return rc;
    }

    bridgectl_log_status(bridge, fd, *mode);

    return 0;
}

static const struct bridgectl_ops ilpcctl_ops = {
    .name = ilpcctl_name,
    .enforce = ilpcctl_enforce,
    .status = ilpcctl_status,
    .report = ilpcctl_report,
};

static const struct soc_device_id ilpcctl_matches[] = {
    { .compatible = "aspeed,ast2400-ilpc-ahb-bridge", },
    { .compatible = "aspeed,ast2500-ilpc-ahb-bridge", },
    { },
};

static int ilpcctl_driver_init(struct soc *soc, struct soc_device *dev)
{
    struct ilpcctl *ctx;
    int rc;

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return -ENOMEM;
    }

    if ((rc = soc_device_get_memory(soc, &dev->node, &ctx->lpc)) < 0) {
        goto cleanup_ctx;
    }

    if (!(ctx->sioctl = sioctl_get(soc))) {
        loge("Failed to acquire SuperIO controller\n");
        goto cleanup_ctx;
    }

    ctx->soc = soc;

    soc_device_set_drvdata(dev, ctx);

    if ((rc = soc_bridge_controller_register(soc, ilpcctl_as_bridgectl(ctx), &ilpcctl_ops)) < 0) {
        goto cleanup_drvdata;
    }

    return 0;

cleanup_drvdata:
    soc_device_set_drvdata(dev, NULL);

cleanup_ctx:
    free(ctx);

    return rc;
}

static void ilpcctl_driver_destroy(struct soc_device *dev)
{
    struct ilpcctl *ctx = soc_device_get_drvdata(dev);

    soc_bridge_controller_unregister(ctx->soc, ilpcctl_as_bridgectl(ctx));

    free(ctx);
}

static const struct soc_driver ilpcctl_driver = {
    .name = "ilpcctl",
    .matches = ilpcctl_matches,
    .init = ilpcctl_driver_init,
    .destroy = ilpcctl_driver_destroy,
};
REGISTER_SOC_DRIVER(ilpcctl_driver);
