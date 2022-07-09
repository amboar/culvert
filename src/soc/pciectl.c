// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2022 IBM Corp.

#include "compiler.h"
#include "log.h"
#include "soc/pciectl.h"
#include "soc/sdmc.h"

#include "ccan/container_of/container_of.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SCU_MISC			0x02c
#define   G4_SCU_MISC_P2A_DRAM_RO	(1 << 25)
#define   G4_SCU_MISC_P2A_SPI_RO	(1 << 24)
#define   G4_SCU_MISC_P2A_SOC_RO	(1 << 23)
#define   G4_SCU_MISC_P2A_FMC_RO	(1 << 22)
#define   G5_SCU_MISC_P2A_DRAM_RO	(1 << 25)
#define   G5_SCU_MISC_P2A_LPCH_RO	(1 << 24)
#define   G5_SCU_MISC_P2A_SOC_RO	(1 << 23)
#define   G5_SCU_MISC_P2A_FLASH_RO	(1 << 22)
#define SCU_PCIE_CONFIG		        0x180
#define   SCU_PCIE_CONFIG_BMC_XDMA      (1 << 14)
#define   SCU_PCIE_CONFIG_BMC_MMIO	(1 << 9)
#define   SCU_PCIE_CONFIG_BMC		(1 << 8)
#define   SCU_PCIE_CONFIG_VGA_XDMA      (1 << 6)
#define   SCU_PCIE_CONFIG_VGA_MMIO	(1 << 1)
#define   SCU_PCIE_CONFIG_VGA		(1 << 0)

static const char *pcie_device_table[] = {
    [pcie_device_vga] = "VGA",
    [pcie_device_bmc] = "BMC",
};

enum device_function { device_function_none = 0, device_function_mmio, device_function_xdma };

static const char* device_function_table[] = {
    [device_function_none] = "none",
    [device_function_mmio] = "MMIO",
    [device_function_xdma] = "XDMA",
};

struct pciectl_p2a_region {
    const char *name;
    uint32_t mask;
    uint32_t start;
    uint32_t length;
};

struct pciectl_endpoint {
    struct {
        enum pcie_device type;
        uint32_t mask;
    } device;

    struct {
        enum device_function type;
        uint32_t mask;
    } function;
};

struct pciectl_pdata {
    const struct pciectl_endpoint *endpoints;
    const struct pciectl_p2a_region *regions;
};

struct pciectl {
    struct bridgectl p2actl;
    struct bridgectl xdmactl;
    struct soc *soc;
    struct soc_region scu;
    const struct pciectl_pdata *pdata;
    struct sdmc *sdmc;
};
#define p2actl_to_pciectl(b) container_of((b), struct pciectl, p2actl)
#define xdmactl_to_pciectl(b) container_of((b), struct pciectl, xdmactl)

struct bridgectl *p2actl_as_bridgectl(struct pciectl *ctx)
{
    return &ctx->p2actl;
}

struct bridgectl *xdmactl_as_bridgectl(struct pciectl *ctx)
{
    return &ctx->xdmactl;
}

static uint32_t pciectl_pdata_collect_region_mask(const struct pciectl_pdata *pdata,
                                                  const struct pciectl_endpoint *ep)
{
    const struct pciectl_p2a_region *curr;
    uint32_t mask = 0;

    if (ep->function.type != device_function_mmio) {
        return 0;
    }

    curr = pdata->regions;

    while(curr && curr->name) {
        mask |= curr->mask;
        curr++;
    }

    return mask;
}

static int
pciectl_device_enforce(struct pciectl *ctx, const struct pciectl_endpoint *ep, enum bridge_mode mode)
{
    uint32_t pcie, misc;
    uint32_t mask;
    int rc;

    if ((rc = soc_readl(ctx->soc, ctx->scu.start + SCU_PCIE_CONFIG, &pcie)) < 0) {
        loge("Failed to read PCIe device configuration: %d\n", rc);
        return rc;
    }

    if (mode == bm_disabled) {
        pcie &= ~ep->function.mask;

        if ((rc = soc_writel(ctx->soc, ctx->scu.start + SCU_PCIE_CONFIG, pcie)) < 0) {
            loge("Failed to disable PCIe MMIO regions: %d\n", rc);
        }

        return rc;
    }

    if ((rc = soc_readl(ctx->soc, ctx->scu.start + SCU_MISC, &misc)) < 0) {
        loge("Failed to read PCIe MMIO region configuration: %d\n", rc);
        return rc;
    }

    mask = pciectl_pdata_collect_region_mask(ctx->pdata, ep);
    if (!mask) {
        return 0;
    }

    if (mode == bm_restricted) {
        misc |= mask;
    } else {
        assert(mode == bm_permissive);
        misc &= ~mask;
    }

    if ((rc = soc_writel(ctx->soc, ctx->scu.start + SCU_MISC, misc)) < 0) {
        loge("Failed to write PCIe MMIO region configuration: %d\n", rc);
        return rc;
    }

    pcie |= ep->device.mask | ep->function.mask;
    if ((rc = soc_writel(ctx->soc, ctx->scu.start + SCU_PCIE_CONFIG, pcie)) < 0) {
        loge("Failed to enable PCIe VGA device: %d\n", rc);
    }

    return rc;
}

static int
pciectl_device_status(struct pciectl *ctx, const struct pciectl_endpoint *ep, enum bridge_mode *mode)
{
    uint32_t pcie, misc;
    uint32_t mask;
    int rc;

    if ((rc = soc_readl(ctx->soc, ctx->scu.start + SCU_PCIE_CONFIG, &pcie)) < 0) {
        loge("Failed to read PCIe device configuration: %d\n", rc);
        return rc;
    }

    mask = ep->device.mask | ep->function.mask;
    if (!((pcie & mask) == mask)) {
        *mode = bm_disabled;
        return 0;
    }

    mask = pciectl_pdata_collect_region_mask(ctx->pdata, ep);
    if (!mask) {
        *mode = bm_permissive;
        return 0;
    }

    if ((rc = soc_readl(ctx->soc, ctx->scu.start + SCU_MISC, &misc)) < 0) {
        loge("Failed to read PCIe MMIO region configuration: %d\n", rc);
        return rc;
    }

    *mode = ((misc & mask) == mask) ? bm_restricted : bm_permissive;

    return 0;
}

static int
pciectl_device_report(struct pciectl *ctx, int fd, const struct pciectl_endpoint *ep)
{
    const char *description;
    uint32_t pcie;
    bool enabled;
    int rc;

    if ((rc = soc_readl(ctx->soc, ctx->scu.start + SCU_PCIE_CONFIG, &pcie)) < 0) {
        loge("Failed to read PCIe device configuration: %d\n", rc);
        return rc;
    }

    /* Device */
    enabled = pcie & ep->device.mask;
    description = enabled ? "Enabled" : "Disabled";
    dprintf(fd, "\t%s: %s\n", pcie_device_table[ep->device.type], description);
    if (!enabled) {
        return 0;
    }

    /* Function on device */
    enabled = pcie & ep->function.mask;
    description = enabled ? "Enabled" : "Disabled";
    dprintf(fd, "\t%s on %s: %s\n", device_function_table[ep->function.type],
            pcie_device_table[ep->device.type], description);

    return 0;
}

static int
pciectl_enforce(struct pciectl *ctx, enum bridge_mode mode, enum device_function function)
{
    const struct pciectl_endpoint *curr;
    int rc;

    for (curr = ctx->pdata->endpoints;
            curr->function.type != device_function_none;
            curr++) {
        if (curr->function.type != function) {
            continue;
        }

        if ((rc = pciectl_device_enforce(ctx, curr, mode)) < 0) {
            loge("Failed to enforce bridge state for %s %s: %d\n",
                 pcie_device_table[curr->device.type], device_function_table[curr->function.type], rc);
            return rc;
        }
    }

    return 0;
}

static int
pciectl_status(struct pciectl *ctx, enum bridge_mode *mode, enum device_function function)
{
    enum bridge_mode curr_mode, aggregate_mode;
    const struct pciectl_endpoint *curr;
    int rc;

    aggregate_mode = bm_disabled;
    for (curr = ctx->pdata->endpoints;
            curr->function.type != device_function_none;
            curr++) {
        if (curr->function.type != function) {
            continue;
        }

        if ((rc = pciectl_device_status(ctx, curr, &curr_mode)) < 0) {
            loge("Failed to query bridge state for %s %s: %d\n",
                 pcie_device_table[curr->device.type], device_function_table[curr->function.type], rc);
            return rc;
        }

        if (curr_mode < aggregate_mode) {
            aggregate_mode = curr_mode;
        }
    }

    *mode = aggregate_mode;

    return 0;
}

static int pciectl_report(struct pciectl *ctx, int fd, enum device_function function)
{
    const struct pciectl_endpoint *curr;
    int rc;

    for (curr = ctx->pdata->endpoints;
            curr->function.type != device_function_none;
            curr++) {
        if (curr->function.type != function) {
            continue;
        }

        if ((rc = pciectl_device_report(ctx, fd, curr)) < 0) {
            loge("Failed to enforce bridge state for %s %s: %d\n",
                 pcie_device_table[curr->device.type], device_function_table[curr->function.type], rc);
            return rc;
        }
    }

    return 0;
}

static const char *p2actl_name(struct bridgectl *bridge __unused)
{
    return "p2a";
}

static int p2actl_enforce(struct bridgectl *bridge, enum bridge_mode mode)
{
    return pciectl_enforce(p2actl_to_pciectl(bridge), mode, device_function_mmio);
}

static int p2actl_status(struct bridgectl *bridge, enum bridge_mode *mode)
{
    return pciectl_status(p2actl_to_pciectl(bridge), mode, device_function_mmio);
}

static int p2actl_report(struct bridgectl *bridge, int fd, enum bridge_mode *mode)
{
    struct pciectl *ctx = p2actl_to_pciectl(bridge);
    const struct pciectl_p2a_region *curr;
    uint32_t misc;
    int rc;

    if ((rc = p2actl_status(bridge, mode)) < 0) {
        loge("Failed to read P2A bridge status: %d\n", rc);
        return rc;
    }

    bridgectl_log_status(bridge, fd, *mode);

    if (*mode == bm_disabled) {
        return 0;
    }

    if ((rc = pciectl_report(ctx, fd, device_function_mmio)) < 0) {
        loge("Failed to report P2A bridge state: %d\n", rc);
        return rc;
    }

    if ((rc = soc_readl(ctx->soc, ctx->scu.start + SCU_MISC, &misc)) < 0) {
        loge("Failed to read P2A region filter configuration: %d\n", rc);
        return rc;
    }

    curr = ctx->pdata->regions;
    while (curr && curr->name) {
        const char *permission;
        uint32_t end;

        permission = (misc & curr->mask) ? "Readable" : "Writable";
        end = curr->start + (curr->length - 1);

        dprintf(fd, "\t[0x%08x - 0x%08x] %10s: %s\n", curr->start, end, curr->name, permission);

        curr++;
    }

    return 0;
}

static const struct bridgectl_ops p2actl_ops = {
    .name = p2actl_name,
    .enforce = p2actl_enforce,
    .status = p2actl_status,
    .report = p2actl_report,
};

static const char *xdmactl_name(struct bridgectl *bridge __unused)
{
    return "xdma";
}

static int xdmactl_enforce(struct bridgectl *bridge, enum bridge_mode mode)
{
    struct pciectl *ctx = xdmactl_to_pciectl(bridge);
    int rc;

    if ((rc = sdmc_configure_xdma(ctx->sdmc, (mode == bm_permissive))) < 0) {
        loge("Failed to configure XDMA constraint in SDMC: %d\n", rc);
        return rc;
    }

    return pciectl_enforce(ctx, mode, device_function_xdma);
}

static int xdmactl_status(struct bridgectl *bridge, enum bridge_mode *mode)
{
    struct pciectl *ctx = xdmactl_to_pciectl(bridge);
    int rc;

    if ((rc = pciectl_status(xdmactl_to_pciectl(bridge), mode, device_function_xdma)) < 0) {
        loge("Failed to assess XDMA bridge status: %d\n", rc);
        return rc;
    }

    if ((rc = sdmc_constrains_xdma(ctx->sdmc)) < 0) {
        loge("Failed to assess XDMA access contraint: %d\n", rc);
        return rc;
    }

    if (*mode == bm_permissive && rc) {
        *mode = bm_restricted;
    }

    return 0;
}

static int xdmactl_report(struct bridgectl *bridge, int fd, enum bridge_mode *mode)
{
    struct pciectl *ctx = xdmactl_to_pciectl(bridge);
    int rc;

    if ((rc = xdmactl_status(bridge, mode)) < 0) {
        loge("Failed to read P2A bridge status: %d\n", rc);
        return rc;
    }

    bridgectl_log_status(bridge, fd, *mode);

    if (*mode == bm_disabled) {
        return 0;
    }

    if ((rc = pciectl_report(ctx, fd, device_function_xdma)) < 0) {
        loge("Failed to report P2A bridge state: %d\n", rc);
        return rc;
    }

    if ((rc = sdmc_constrains_xdma(ctx->sdmc)) < 0) {
        loge("Failed to access XDMA access contraint: %d\n", rc);
        return rc;
    }

    dprintf(fd, "\tXDMA is constrained: %s\n", rc ? "Yes" : "No");

    return 0;
}


static const struct bridgectl_ops xdmactl_ops = {
    .name = xdmactl_name,
    .enforce = xdmactl_enforce,
    .status = xdmactl_status,
    .report = xdmactl_report,
};

static const struct pciectl_p2a_region ast2400_p2a_regions[] = {
    {
        .name = "Firmware",
        .mask = G4_SCU_MISC_P2A_FMC_RO,
        .start = 0,
        .length = 0x18000000,
    },
    {
        .name = "SoC IO",
        .mask = G4_SCU_MISC_P2A_SOC_RO,
        .start = 0x18000000,
        .length = 0x08000000,
    },
    {
        .name = "BMC Flash",
        .mask = G4_SCU_MISC_P2A_FMC_RO,
        .start = 0x20000000,
        .length = 0x10000000,
    },
    {
        .name = "Host Flash",
        .mask = G4_SCU_MISC_P2A_SPI_RO,
        .start = 0x30000000,
        .length = 0x10000000,
    },
    {
        .name = "DRAM",
        .mask = G4_SCU_MISC_P2A_DRAM_RO,
        .start = 0x40000000,
        .length = 0x20000000,
    },
    {
        .name = "LPC Host",
        .mask = G4_SCU_MISC_P2A_SOC_RO,
        .start = 0x60000000,
        .length = 0x20000000,
    },
    {
        .name = "Reserved",
        .mask = G4_SCU_MISC_P2A_SOC_RO,
        .start = 0x80000000,
        .length = 0x80000000,
    },
    { },
};

static const struct pciectl_endpoint ast2400_pcie_bridges[] = {
    {
        .device = {
            .type = pcie_device_bmc,
            .mask = SCU_PCIE_CONFIG_BMC,
        },
        .function = {
            .type = device_function_mmio,
            .mask = SCU_PCIE_CONFIG_BMC_MMIO,
        },
    },
    {
        .device = {
            .type = pcie_device_vga,
            .mask = SCU_PCIE_CONFIG_VGA,
        },
        .function = {
            .type = device_function_mmio,
            .mask = SCU_PCIE_CONFIG_VGA_MMIO,
        },
    },
    {
        .device = {
            .type = pcie_device_bmc,
            .mask = SCU_PCIE_CONFIG_BMC,
        },
        .function = {
            .type = device_function_xdma,
            .mask = SCU_PCIE_CONFIG_BMC_XDMA,
        }
    },
    {
        .device = {
            .type = pcie_device_vga,
            .mask = SCU_PCIE_CONFIG_VGA,
        },
        .function = {
            .type = device_function_xdma,
            .mask = SCU_PCIE_CONFIG_VGA_XDMA,
        },
    },
    { }
};

static const struct pciectl_pdata ast2400_pdata = {
    .endpoints = ast2400_pcie_bridges,
    .regions = ast2400_p2a_regions,
};

static const struct pciectl_p2a_region ast2500_p2a_regions[] = {
    {
        .name = "Firmware",
        .mask = G5_SCU_MISC_P2A_FLASH_RO,
        .start = 0,
        .length = 0x10000000,
    },
    {
        .name = "SoC IO",
        .mask = G5_SCU_MISC_P2A_SOC_RO,
        .start = 0x10000000,
        .length = 0x10000000,
    },
    {
        .name = "BMC Flash",
        .mask = G5_SCU_MISC_P2A_FLASH_RO,
        .start = 0x20000000,
        .length = 0x10000000,
    },
    {
        .name = "Host Flash",
        .mask = G5_SCU_MISC_P2A_FLASH_RO,
        .start = 0x30000000,
        .length = 0x10000000,
    },
    {
        .name = "Reserved",
        .mask = G5_SCU_MISC_P2A_SOC_RO,
        .start = 0x40000000,
        .length = 0x20000000,
    },
    {
        .name = "LPC Host",
        .mask = G5_SCU_MISC_P2A_LPCH_RO,
        .start = 0x60000000,
        .length = 0x20000000,
    },
    {
        .name = "DRAM",
        .mask = G5_SCU_MISC_P2A_DRAM_RO,
        .start = 0x80000000,
        .length = 0x80000000,
    },
    { },
};

static const struct pciectl_endpoint ast2500_pcie_bridges[] = {
    {
        .device = {
            .type = pcie_device_bmc,
            .mask = SCU_PCIE_CONFIG_BMC,
        },
        .function = {
            .type = device_function_mmio,
            .mask = SCU_PCIE_CONFIG_BMC_MMIO,
        },
    },
    {
        .device = {
            .type = pcie_device_vga,
            .mask = SCU_PCIE_CONFIG_VGA,
        },
        .function = {
            .type = device_function_mmio,
            .mask = SCU_PCIE_CONFIG_VGA_MMIO,
        },
    },
    {
        .device = {
            .type = pcie_device_bmc,
            .mask = SCU_PCIE_CONFIG_BMC,
        },
        .function = {
            .type = device_function_xdma,
            .mask = SCU_PCIE_CONFIG_BMC_XDMA,
        },
    },
    {
        .device = {
            .type = pcie_device_vga,
            .mask = SCU_PCIE_CONFIG_VGA,
        },
        .function = {
            .type = device_function_xdma,
            .mask = SCU_PCIE_CONFIG_VGA_XDMA,
        },
    },
    { }
};

static const struct pciectl_pdata ast2500_pdata = {
    .endpoints = ast2500_pcie_bridges,
    .regions = ast2500_p2a_regions,
};

static const struct soc_device_id pciectl_matches[] = {
    { .compatible = "aspeed,ast2400-pcie-device-controller", .data = &ast2400_pdata },
    { .compatible = "aspeed,ast2500-pcie-device-controller", .data = &ast2500_pdata },
    { },
};

static int pciectl_driver_init(struct soc *soc, struct soc_device *dev)
{
    struct pciectl *ctx;
    int rc;

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return -ENOMEM;
    }

    if ((rc = soc_device_get_memory(soc, &dev->node, &ctx->scu)) < 0) {
        goto cleanup_ctx;
    }

    if (!(ctx->sdmc = sdmc_get(soc))) {
        loge("Failed to acquire SDMC controller\n");
        goto cleanup_ctx;
    }

    ctx->soc = soc;
    ctx->pdata = soc_device_get_match_data(soc, pciectl_matches, &dev->node);

    soc_device_set_drvdata(dev, ctx);

    if ((rc = soc_bridge_controller_register(soc, p2actl_as_bridgectl(ctx), &p2actl_ops)) < 0) {
        goto cleanup_drvdata;
    }

    if ((rc = soc_bridge_controller_register(soc, xdmactl_as_bridgectl(ctx), &xdmactl_ops)) < 0) {
        goto cleanup_drvdata;
    }

    return 0;

cleanup_drvdata:
    soc_device_set_drvdata(dev, NULL);

cleanup_ctx:
    free(ctx);

    return rc;
}

static void pciectl_driver_destroy(struct soc_device *dev)
{
    struct pciectl *ctx = soc_device_get_drvdata(dev);

    soc_bridge_controller_unregister(ctx->soc, p2actl_as_bridgectl(ctx));

    free(ctx);
}

static const struct soc_driver pciectl_driver = {
    .name = "pciectl",
    .matches = pciectl_matches,
    .init = pciectl_driver_init,
    .destroy = pciectl_driver_destroy,
};
REGISTER_SOC_DRIVER(pciectl_driver);
