// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021 IBM Corp.

#include "array.h"
#include "log.h"
#include "soc/bridge-ids.h"
#include "soc/bridges.h"

#include <libfdt.h>

#include <errno.h>
#include <endian.h>
#include <inttypes.h>

struct bridge_gate_desc {
    uint32_t reg;
    uint32_t mask;
};

struct bridge_gate_pdata {
    const struct bridge_gate_desc *descs;
    size_t ndescs;
};

#define AST2500_SCU_MISC                0x02c
#define   AST2500_SCU_MISC_UART_DBG     (1 << 10)

static const struct bridge_gate_desc ast2500_bridge_gates[] = {
    [AST2500_DEBUG_UART_GATE] = { .reg = AST2500_SCU_MISC, .mask = AST2500_SCU_MISC_UART_DBG },
};

static const struct bridge_gate_pdata ast2500_bridge_pdata = {
    .descs = ast2500_bridge_gates,
    .ndescs = ARRAY_SIZE(ast2500_bridge_gates),
};

#define AST2600_SCU_DBGCTL1             0x0c8
#define   AST2600_SCU_DBGCTL1_UART5_DBG (1 << 1)
#define AST2600_SCU_DBGCTL2             0x0d8
#define   AST2600_SCU_DBGCTL2_UART1_DBG (1 << 3)

static const struct bridge_gate_desc ast2600_bridge_gates[] = {
    [AST2600_DEBUG_UART1_GATE] =
        { .reg = AST2600_SCU_DBGCTL2, .mask = AST2600_SCU_DBGCTL2_UART1_DBG },
    [AST2600_DEBUG_UART5_GATE] =
        { .reg = AST2600_SCU_DBGCTL1, .mask = AST2600_SCU_DBGCTL1_UART5_DBG },
};

static const struct bridge_gate_pdata ast2600_bridge_pdata = {
    .descs = ast2600_bridge_gates,
    .ndescs = ARRAY_SIZE(ast2600_bridge_gates),
};

static const struct soc_device_id bridges_matches[] = {
    { .compatible = "aspeed,ast2500-bridge-controller", .data = &ast2500_bridge_pdata },
    { .compatible = "aspeed,ast2600-bridge-controller", .data = &ast2600_bridge_pdata },
    { },
};

struct bridges {
    struct soc *soc;
    struct soc_region scu;
    const struct bridge_gate_pdata *pdata;
};

int bridges_driver_init(struct soc *soc, struct soc_device *dev)
{
    struct bridges *ctx;
    int rc;

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return -ENOMEM;
    }

    if ((rc = soc_device_get_memory(soc, &dev->node, &ctx->scu)) < 0) {
        goto cleanup_ctx;
    }

    ctx->soc = soc;
    ctx->pdata = soc_device_get_match_data(soc, bridges_matches, &dev->node);

    soc_device_set_drvdata(dev, ctx);

    return 0;

cleanup_ctx:
    free(ctx);

    return rc;
}

static void bridges_driver_destroy(struct soc_device *dev)
{
    free(soc_device_get_drvdata(dev));
}

static const struct soc_driver bridges_driver = {
    .name = "bridge-controller",
    .matches = bridges_matches,
    .init = bridges_driver_init,
    .destroy = bridges_driver_destroy,
};
REGISTER_SOC_DRIVER(bridges_driver);

struct bridges *bridges_get(struct soc *ctx)
{
    return soc_driver_get_drvdata(ctx, &bridges_driver);
}

int bridges_device_get_gates(struct soc *soc, struct soc_device *dev, struct bridges **bridges,
                             int *gate)
{
    struct soc_device_node bdn;
    struct bridges *_bridges;
    const uint32_t *cell;
    int phandle;
    int offset;
    int len;

    cell = fdt_getprop(soc->fdt.start, dev->node.offset, "bridge-gates", &len);
    if (len < 0) {
        loge("Failed to find 'bridge-gates' property in node %d: %d\n", dev->node.offset, len);
        return -ERANGE;
    }

    if (len < 2) {
        loge("Invalid value for 'bridge-gates' property, must be <phandle index [index...]>\n");
        return -EINVAL;
    }

    phandle = be32toh(cell[0]);
    offset = fdt_node_offset_by_phandle(soc->fdt.start, phandle);
    if (offset < 0) {
            loge("fdt: Failed to find node for phandle %"PRIu32" at index %d: %d\n",
                 phandle, 0, offset);
            return -EUCLEAN;
    }

    bdn.fdt = &soc->fdt;
    bdn.offset = offset;

    _bridges = soc_driver_get_drvdata_by_node(soc, &bdn);
    if (!_bridges) {
        loge("Failed to locate bridge controller instance\n");
        return -ENODEV;
    }

    *bridges = _bridges;
    *gate = be32toh(cell[1]);

    return 0;
}

static int bridges_configure(struct bridges *ctx, int bridge, bool enable)
{
    const struct bridge_gate_desc *desc;
    uint32_t phys, val;
    int rc;

    if (bridge < 0) {
        logd("Invalid bridge identifier: %d\n", bridge);
        return -EINVAL;
    }

    if ((size_t)bridge >= ctx->pdata->ndescs) {
        logd("Invalid bridge identifier: %d (%d)\n", bridge, ctx->pdata->ndescs);
        return -EINVAL;
    }

    desc = &ctx->pdata->descs[bridge];
    phys = ctx->scu.start + desc->reg;
    if ((rc = soc_readl(ctx->soc, phys, &val)) < 0) {
        loge("Failed to read bridge control register 0x%"PRIx32": %d\n", phys, rc);
        return rc;
    }

    /* Bridge control registers tend to set bits to disable the bridge */
    if (enable) {
        val &= ~desc->mask;
    } else {
        val |= desc->mask;
    }

    if ((rc = soc_writel(ctx->soc, phys, val)) < 0) {
        loge("Failed to write 0x%"PRIx32" to bridge control register 0x%"PRIx32": %d\n", phys, rc);
        return rc;
    }

    return 0;
}

int bridges_enable(struct bridges *ctx, int bridge)
{
    return bridges_configure(ctx, bridge, true);
}

int bridges_disable(struct bridges *ctx, int bridge)
{
    return bridges_configure(ctx, bridge, false);
}

int bridges_status(struct bridges *ctx, int bridge)
{
    const struct bridge_gate_desc *desc;
    uint32_t phys, val;
    int rc;

    if (bridge < 0) {
        logd("Invalid bridge identifier: %d\n", bridge);
        return -EINVAL;
    }

    if ((size_t)bridge >= ctx->pdata->ndescs) {
        logd("Invalid bridge identifier: %d (%d)\n", bridge, ctx->pdata->ndescs);
        return -EINVAL;
    }

    desc = &ctx->pdata->descs[bridge];
    phys = ctx->scu.start + desc->reg;
    if ((rc = soc_readl(ctx->soc, phys, &val)) < 0) {
        loge("Failed to read bridge control register 0x%"PRIx32": %d\n", phys, rc);
        return rc;
    }

    /* Bridge control registers tend to set bits to disable the bridge */
    return !(val & desc->mask);
}
