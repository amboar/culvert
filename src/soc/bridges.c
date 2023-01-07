// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021 IBM Corp.

#include "array.h"
#include "log.h"
#include "soc/bridge-ids.h"
#include "soc/bridges.h"

#include <libfdt.h>

#include <assert.h>
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
#define   AST2600_SCU_DBGCTL1_XDMA_VGA  (1 << 8)
#define   AST2600_SCU_DBGCTL1_XDMA      (1 << 2)
#define   AST2600_SCU_DBGCTL1_UART5_DBG (1 << 1)
#define   AST2600_SCU_DBGCTL1_P2A       (1 << 0)
#define AST2600_SCU_DBGCTL2             0x0d8
#define   AST2600_SCU_DBGCTL2_UART1_DBG (1 << 3)

static const struct bridge_gate_desc ast2600_bridge_gates[] = {
    [AST2600_DEBUG_UART1_GATE] =
        { .reg = AST2600_SCU_DBGCTL2, .mask = AST2600_SCU_DBGCTL2_UART1_DBG },
    [AST2600_DEBUG_UART5_GATE] =
        { .reg = AST2600_SCU_DBGCTL1, .mask = AST2600_SCU_DBGCTL1_UART5_DBG },
    [AST2600_P2A_GATE] =
        { .reg = AST2600_SCU_DBGCTL1, .mask = AST2600_SCU_DBGCTL1_P2A },
    [AST2600_XDMA_GATE] =
        { .reg = AST2600_SCU_DBGCTL1, .mask = AST2600_SCU_DBGCTL1_XDMA },
    [AST2600_XDMA_VGA_GATE] =
        { .reg = AST2600_SCU_DBGCTL1, .mask = AST2600_SCU_DBGCTL1_XDMA_VGA },
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

static struct bridges *bridges_get_by_prop(struct soc *soc, const uint32_t *cells)
{
    struct soc_device_node bdn;
    int phandle;
    int offset;

    assert(cells);

    phandle = be32toh(cells[0]);
    offset = fdt_node_offset_by_phandle(soc->fdt.start, phandle);
    if (offset < 0) {
        loge("fdt: Failed to find node for phandle %"PRIu32" at index %d: %d\n",
                phandle, 0, offset);
        return NULL;
    }

    bdn.fdt = &soc->fdt;
    bdn.offset = offset;

    return soc_driver_get_drvdata_by_node(soc, &bdn);
}

struct bridges *bridges_get_by_device(struct soc *soc, struct soc_device *dev)
{
    const uint32_t *cells;
    int len;

    cells = fdt_getprop(soc->fdt.start, dev->node.offset, "bridge-gates", &len);
    if (len < 0) {
        loge("Failed to find 'bridge-gates' property in node %d: %d\n", dev->node.offset, len);
        return NULL;
    }

    if ((long unsigned int)len < 2 * sizeof(*cells)) {
        loge("Invalid value for 'bridge-gates' property, must be <phandle index [index...]>\n");
        return NULL;
    }

    return bridges_get_by_prop(soc, cells);
}

int bridges_device_get_gate_by_index(struct soc *soc, struct soc_device *dev, int index,
                                     struct bridges **bridges, int *gate)
{
    const uint32_t *cells;
    int len;

    if (!gate) {
        loge("'gate' parameter must point to a valid int object\n");
        return -EINVAL;
    }

    cells = fdt_getprop(soc->fdt.start, dev->node.offset, "bridge-gates", &len);
    if (len < 0) {
        loge("Failed to find 'bridge-gates' property in node %d: %d\n", dev->node.offset, len);
        return -ERANGE;
    }

    if ((long unsigned int)len < 2 * sizeof(*cells)) {
        loge("Invalid value for 'bridge-gates' property, must be <phandle index [index...]>\n");
        return -EINVAL;
    }

    if ((long unsigned int)len <= (1 + index) * sizeof(*cells)) {
        loge("Invalid index for 'bridge-gates' property: %d (%d)\n", index, (len / sizeof(*cells)));
        return -EINVAL;
    }

    if (bridges) {
        struct bridges *_bridges;

        if (!(_bridges = bridges_get_by_prop(soc, cells))) {
            return -EINVAL;
        }

        *bridges = _bridges;
    }

    *gate = be32toh(cells[1 + index]);

    return 0;
}

int bridges_device_get_gate(struct soc *soc, struct soc_device *dev, struct bridges **bridges,
                             int *gate)
{
    return bridges_device_get_gate_by_index(soc, dev, 0, bridges, gate);
}

int bridges_device_get_gate_by_name(struct soc *soc, struct soc_device *dev, const char *name,
                                    struct bridges **bridges, int *gate)
{
    int idx;
    int rc;

    idx = fdt_stringlist_search(soc->fdt.start, dev->node.offset, "bridge-gate-names", name);
    if (idx < 0) {
        loge("Failed to find 'bridge-gate-names' node: %d\n", idx);
        return -EINVAL;
    }

    rc = bridges_device_get_gate_by_index(soc, dev, idx, bridges, gate);
    if (!rc) {
        logt("Resolved bridge gate name '%s' to ID %d via device node %d\n",
             name, *gate, dev->node.offset);
    }

    return rc;
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
