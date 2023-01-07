// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2022 IBM Corp.

#include "compiler.h"
#include "log.h"
#include "soc/bridges.h"
#include "soc/debugctl.h"

#include "ccan/container_of/container_of.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SCU_MISC                0x02c
#define   SCU_MISC_UART_DBG     (1 << 10)
#define SCU_STRAP               0x070
#define   SCU_STRAP_DBG_SEL     (1 << 29)

struct debugctl {
    struct bridgectl ctl;
    struct soc *soc;
    struct soc_region region;
    struct bridges *bridges;
    int id;
};
#define to_debugctl(b) container_of((b), struct debugctl, ctl)

struct bridgectl *debugctl_as_bridgectl(struct debugctl *ctx)
{
    return &ctx->ctl;
}

static const char *debugctl_name(struct bridgectl *bridge __unused)
{
    return "debug";
}

static int debugctl_enforce(struct bridgectl *bridge, enum bridge_mode mode)
{
    struct debugctl *ctx = to_debugctl(bridge);

    if (mode == bm_disabled) {
        return bridges_disable(ctx->bridges, ctx->id);
    }

    /* The debug UART doesn't support restricted mode */
    return bridges_enable(ctx->bridges, ctx->id);
}

static int debugctl_status(struct bridgectl *bridge, enum bridge_mode *mode)
{
    struct debugctl *ctx = to_debugctl(bridge);
    int rc;

    if ((rc = bridges_status(ctx->bridges, ctx->id)) < 0) {
        loge("Failed to read debug controller status: %d\n", rc);
        return rc;
    }

    *mode = rc ? bm_permissive : bm_disabled;

    return 0;
}

static int ast2500_debugctl_report(struct bridgectl *bridge, int fd, enum bridge_mode *mode)
{
    struct debugctl *ctx = to_debugctl(bridge);
    const char *description;
    uint32_t strap;
    int rc;

    if ((rc = debugctl_status(bridge, mode)) < 0) {
        loge("Failed to read debug UART bridge status: %d\n", rc);
        return rc;
    }

    bridgectl_log_status(bridge, fd, *mode);

    if (*mode == bm_disabled) {
        return 0;
    }

    if ((rc = soc_readl(ctx->soc, ctx->region.start + SCU_STRAP, &strap)) < 0) {
        loge("Failed to read debug UART port state: %d\n", rc);
        return rc;
    }

    description = (strap & SCU_STRAP_DBG_SEL) ? "UART5" : "UART1";
    dprintf(fd, "\tDebug UART port: %s\n", description);

    return 0;
}

static const struct bridgectl_ops ast2500_debugctl_ops = {
    .name = debugctl_name,
    .enforce = debugctl_enforce,
    .status = debugctl_status,
    .report = ast2500_debugctl_report,
};

static int ast2600_debugctl_report(struct bridgectl *bridge, int fd, enum bridge_mode *mode)
{
    struct debugctl *ctx = to_debugctl(bridge);
    int rc;

    if ((rc = debugctl_status(bridge, mode)) < 0) {
        loge("Failed to read debug UART bridge status: %d\n", rc);
        return rc;
    }

    bridgectl_log_status(bridge, fd, *mode);

    switch (ctx->region.start) {
        case 0x1e783000:
            dprintf(fd, "\tDebug UART port: UART1\n");
            break;
        case 0x1e784000:
            dprintf(fd, "\tDebug UART port: UART5\n");
            break;
        default:
            return -EINVAL;
    }

    return 0;
}

static const struct bridgectl_ops ast2600_debugctl_ops = {
    .name = debugctl_name,
    .enforce = debugctl_enforce,
    .status = debugctl_status,
    .report = ast2600_debugctl_report,
};

static const struct soc_device_id debugctl_matches[] = {
    { .compatible = "aspeed,ast2500-debug-ahb-bridge", .data = &ast2500_debugctl_ops },
    { .compatible = "aspeed,ast2600-uart", .data = &ast2600_debugctl_ops },
    { },
};

static int debugctl_driver_init(struct soc *soc, struct soc_device *dev)
{
    const struct bridgectl_ops *ops;
    struct debugctl *ctx;
    int rc;

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return -ENOMEM;
    }

    if ((rc = soc_device_get_memory(soc, &dev->node, &ctx->region)) < 0) {
        goto cleanup_ctx;
    }

    ctx->soc = soc;

    if ((rc = bridges_device_get_gates(soc, dev, &ctx->bridges, &ctx->id)) < 0) {
        goto cleanup_ctx;
    }

    soc_device_set_drvdata(dev, ctx);

    ops = soc_device_get_match_data(soc, debugctl_matches, &dev->node);
    if (!ops) {
        logd("Failed to find device match data\n");
        rc = -EINVAL;
        goto cleanup_drvdata;
    }

    if ((rc = soc_bridge_controller_register(soc, debugctl_as_bridgectl(ctx), ops)) < 0) {
        goto cleanup_drvdata;
    }

    return 0;

cleanup_drvdata:
    soc_device_set_drvdata(dev, NULL);

cleanup_ctx:
    free(ctx);

    return rc;
}

static void debugctl_driver_destroy(struct soc_device *dev)
{
    struct debugctl *ctx = soc_device_get_drvdata(dev);

    soc_bridge_controller_unregister(ctx->soc, debugctl_as_bridgectl(ctx));

    free(ctx);
}

static const struct soc_driver debugctl_driver = {
    .name = "debugctl",
    .matches = debugctl_matches,
    .init = debugctl_driver_init,
    .destroy = debugctl_driver_destroy,
};
REGISTER_SOC_DRIVER(debugctl_driver);
