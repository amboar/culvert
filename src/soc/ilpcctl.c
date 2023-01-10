// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2022 IBM Corp.

#include "compiler.h"
#include "log.h"
#include "soc/bridges.h"
#include "soc/ilpcctl.h"
#include "soc/sioctl.h"

#include "ccan/container_of/container_of.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define LPC_HICRB               0x100
#define   LPC_HICRB_ILPC_DIS    (1 << 29)
#define   LPC_HICRB_ILPC_RO     (1 << 6)

struct ilpcctl {
    struct bridgectl ctl;
    struct soc *soc;
    struct soc_region lpc;
    struct sioctl *sioctl;
    struct bridges *bridges;
    int gate;
};
#define to_ilpcctl(b) container_of((b), struct ilpcctl, ctl)

struct bridgectl *ilpcctl_as_bridgectl(struct ilpcctl *ctx)
{
    return &ctx->ctl;
}

static const char *ilpcctl_name(struct bridgectl *bridge __unused)
{
    return "ilpc";
}

static int ast2400_ilpcctl_enforce(struct bridgectl *bridge, enum bridge_mode mode)
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

static int ast2400_ilpcctl_status(struct bridgectl *bridge, enum bridge_mode *mode)
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
    struct ilpcctl *ctx = to_ilpcctl(bridge);
    enum sioctl_decode decode;
    int address;
    int rc;

    if ((rc = bridgectl_status(bridge, mode)) < 0) {
        loge("Failed to read iLPC bridge status: %d\n", rc);
        return rc;
    }

    bridgectl_log_status(bridge, fd, *mode);

    if (*mode == bm_disabled) {
        return 0;
    }

    if ((rc = sioctl_decode_status(ctx->sioctl, &decode)) < 0) {
        loge("Failed to get SuperIO decode status: %d\n", rc);
        return rc;
    }

    address = (decode == sioctl_decode_2e) ? 0x2e : 0x4e;
    dprintf(fd, "\tSuperIO address: 0x%02x\n", address);

    return 0;
}

static const struct bridgectl_ops ast2400_ilpcctl_ops = {
    .name = ilpcctl_name,
    .enforce = ast2400_ilpcctl_enforce,
    .status = ast2400_ilpcctl_status,
    .report = ilpcctl_report,
};

static int ast2600_ilpcctl_enforce(struct bridgectl *bridge, enum bridge_mode mode)
{
    struct ilpcctl *ctx = to_ilpcctl(bridge);
    uint32_t hicrb;
    int rc;

    if ((rc = soc_readl(ctx->soc, ctx->lpc.start + LPC_HICRB, &hicrb)) < 0) {
        loge("Failed to read HICRB: %d\n", rc);
        return rc;
    }

    switch (mode) {
        case bm_permissive:
            hicrb &= ~(LPC_HICRB_ILPC_DIS | LPC_HICRB_ILPC_RO);
            break;
        case bm_restricted:
            hicrb &= ~LPC_HICRB_ILPC_DIS;
            hicrb |= LPC_HICRB_ILPC_RO;
            break;
        case bm_disabled:
            hicrb |= LPC_HICRB_ILPC_DIS;
            break;
        default:
            loge("Unrecognised value for mode: %d\n", mode);
            return -EINVAL;
    }

    if ((rc = soc_writel(ctx->soc, ctx->lpc.start + LPC_HICRB, hicrb)) < 0) {
        loge("Failed to write HICRB: %d\n", rc);
        return rc;
    }

    if (mode == bm_permissive || mode == bm_restricted) {
        if ((rc = bridges_enable(ctx->bridges, ctx->gate)) < 0) {
            loge("Failed to ungate iLPC bridge: %d\n", rc);
            return rc;
        }

        /* FIXME: Sort out a way to configure which IO address we use */
        if ((rc = sioctl_decode_configure(ctx->sioctl, sioctl_decode_2e)) < 0) {
            loge("Failed to enable SuperIO decoding on 0x2e: %d\n", rc);
            return rc;
        }
    }

    return 0;
}

static int ast2600_ilpcctl_status(struct bridgectl *bridge, enum bridge_mode *mode)
{
    struct ilpcctl *ctx = to_ilpcctl(bridge);
    enum sioctl_decode decode;
    uint32_t hicrb;
    int rc;

    if ((rc = sioctl_decode_status(ctx->sioctl, &decode)) < 0) {
        loge("Failed to read SuperIO decode status: %d\n", rc);
        return rc;
    } else if (decode == sioctl_decode_disable) {
        *mode = bm_disabled;
        return 0;
    }

    if ((rc = bridges_status(ctx->bridges, ctx->gate)) < 0) {
        loge("Failed to query bridge status for gate %d: %d\n", ctx->gate, rc);
        return rc;
    } else if (!rc) {
        *mode = bm_disabled;
        return 0;
    }

    if ((rc = soc_readl(ctx->soc, ctx->lpc.start + LPC_HICRB, &hicrb)) < 0) {
        loge("Failed to read HICRB: %d\n", rc);
        return rc;
    }

    if (hicrb & LPC_HICRB_ILPC_DIS) {
        *mode = bm_disabled;
        return 0;
    }

    if (hicrb & LPC_HICRB_ILPC_RO) {
        *mode = bm_restricted;
        return 0;
    }

    *mode = bm_permissive;
    return 0;
}

static const struct bridgectl_ops ast2600_ilpcctl_ops = {
    .name = ilpcctl_name,
    .enforce = ast2600_ilpcctl_enforce,
    .status = ast2600_ilpcctl_status,
    .report = ilpcctl_report,
};

static const struct soc_device_id ilpcctl_matches[] = {
    { .compatible = "aspeed,ast2400-ilpc-ahb-bridge", .data = &ast2400_ilpcctl_ops },
    { .compatible = "aspeed,ast2500-ilpc-ahb-bridge", .data = &ast2400_ilpcctl_ops },
    { .compatible = "aspeed,ast2600-ilpc-ahb-bridge", .data = &ast2600_ilpcctl_ops },
    { },
};

static int ilpcctl_driver_init(struct soc *soc, struct soc_device *dev)
{
    const struct bridgectl_ops *ops;
    struct ilpcctl *ctx;
    int rc;

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return -ENOMEM;
    }

    if ((rc = soc_device_get_memory(soc, &dev->node, &ctx->lpc)) < 0) {
        goto cleanup_ctx;
    }

    ops = soc_device_get_match_data(soc, ilpcctl_matches, &dev->node);
    if (!ops) {
        loge("Failed to find ilpcctl ops\n");
        rc = -EINVAL;
        goto cleanup_ctx;
    }

    if (!(ctx->sioctl = sioctl_get(soc))) {
        loge("Failed to acquire SuperIO controller\n");
        goto cleanup_ctx;
    }

    if (ops == &ast2600_ilpcctl_ops) {
        if ((rc = bridges_device_get_gate(soc, dev, &ctx->bridges, &ctx->gate)) < 0) {
            loge("Failed to fetch bridge gate for iLPC bridge: %d\n", rc);
            goto cleanup_ctx;
        }

        logd("iLPC bridge gate ID: %d\n", ctx->gate);
    }

    ctx->soc = soc;

    soc_device_set_drvdata(dev, ctx);

    if ((rc = soc_bridge_controller_register(soc, ilpcctl_as_bridgectl(ctx), ops)) < 0) {
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
