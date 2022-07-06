// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2022 IBM Corp.

#include "compiler.h"
#include "log.h"
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
    struct bridgectl bridge;
    struct soc *soc;
    struct soc_region scu;
};
#define to_debugctl(b) container_of((b), struct debugctl, bridge)

struct bridgectl *debugctl_as_bridgectl(struct debugctl *ctx)
{
    return &ctx->bridge;
}

static const char *debugctl_name(struct bridgectl *bridge __unused)
{
    return "debug";
}

static int debugctl_enforce(struct bridgectl *bridge, enum bridge_mode mode)
{
    struct debugctl *ctx = to_debugctl(bridge);
    uint32_t misc;
    int rc;

    if ((rc = soc_readl(ctx->soc, ctx->scu.start + SCU_MISC, &misc)) < 0) {
        loge("Failed to read debug controller status: %d\n", rc);
        return rc;
    }

    if (mode == bm_disabled) {
        misc |= SCU_MISC_UART_DBG;
    } else {
        /* The debug UART doesn't support restricted mode */
        misc &= ~SCU_MISC_UART_DBG;
    }

    if ((rc = soc_writel(ctx->soc, ctx->scu.start + SCU_MISC, misc)) < 0) {
        loge("Failed to set debug controller status: %d\n", rc);
    }

    return rc;
}

static int debugctl_status(struct bridgectl *bridge, enum bridge_mode *mode)
{
    struct debugctl *ctx = to_debugctl(bridge);
    uint32_t misc;
    int rc;

    if ((rc = soc_readl(ctx->soc, ctx->scu.start + SCU_MISC, &misc)) < 0) {
        loge("Failed to read debug controller status: %d\n", rc);
        return rc;
    }

    *mode = (misc & SCU_MISC_UART_DBG) ? bm_disabled : bm_permissive;

    return 0;
}

static int debugctl_report(struct bridgectl *bridge, int fd, enum bridge_mode *mode)
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

    if ((rc = soc_readl(ctx->soc, ctx->scu.start + SCU_STRAP, &strap)) < 0) {
        loge("Failed to read debug UART port state: %d\n", rc);
        return rc;
    }

    description = (strap & SCU_STRAP_DBG_SEL) ? "UART5" : "UART1";
    dprintf(fd, "\tDebug UART port: %s\n", description);

    return 0;
}

static const struct bridgectl_ops debugctl_ops = {
    .name = debugctl_name,
    .enforce = debugctl_enforce,
    .status = debugctl_status,
    .report = debugctl_report,
};

static int debugctl_driver_init(struct soc *soc, struct soc_device *dev)
{
    struct debugctl *ctx;
    int rc;

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return -ENOMEM;
    }

    if ((rc = soc_device_get_memory(soc, &dev->node, &ctx->scu)) < 0) {
        goto cleanup_ctx;
    }

    ctx->soc = soc;

    soc_device_set_drvdata(dev, ctx);

    if ((rc = soc_bridge_controller_register(soc, debugctl_as_bridgectl(ctx), &debugctl_ops)) < 0) {
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

static const struct soc_device_id debugctl_matches[] = {
    { .compatible = "aspeed,ast2500-debug-ahb-bridge" },
    { },
};

static const struct soc_driver debugctl_driver = {
    .name = "debugctl",
    .matches = debugctl_matches,
    .init = debugctl_driver_init,
    .destroy = debugctl_driver_destroy,
};
REGISTER_SOC_DRIVER(debugctl_driver);
