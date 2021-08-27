// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.
// Copyright (C) 2021, Oracle and/or its affiliates.

#define _GNU_SOURCE
#include "ast.h"
#include "devmem.h"
#include "ilpc.h"
#include "log.h"
#include "mb.h"
#include "p2a.h"
#include "priv.h"
#include "sdmc.h"
#include "soc.h"
#include "rev.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* SCU */
#define SCU_MISC			0x02c
#define   SCU_MISC_G4_P2A_DRAM_RO	(1 << 25)
#define   SCU_MISC_G4_P2A_SPI_RO	(1 << 24)
#define   SCU_MISC_G4_P2A_SOC_RO	(1 << 23)
#define   SCU_MISC_G4_P2A_FMC_RO	(1 << 22)
#define   SCU_MISC_G5_P2A_DRAM_RO	(1 << 25)
#define   SCU_MISC_G5_P2A_LPCH_RO	(1 << 24)
#define   SCU_MISC_G5_P2A_SOC_RO	(1 << 23)
#define   SCU_MISC_G5_P2A_FLASH_RO	(1 << 22)
#define   SCU_MISC_UART_DBG	        (1 << 10)
#define SCU_MISC2		        0x04c
#define   SCU_MISC2_UART_DBG_1M	        (1 << 30)
#define SCU_HW_STRAP			0x070
#define   SCU_HW_STRAP_UART_DBG_SEL	(1 << 29)
#define   SCU_HW_STRAP_SIO_DEC	        (1 << 20)
#define SCU_SILICON_REVISION		0x07c
#define SCU_PCIE_CONFIG		        0x180
#define   SCU_PCIE_CONFIG_BMC_XDMA      (1 << 14)
#define   SCU_PCIE_CONFIG_BMC_MMIO	(1 << 9)
#define   SCU_PCIE_CONFIG_BMC		(1 << 8)
#define   SCU_PCIE_CONFIG_VGA_XDMA      (1 << 6)
#define   SCU_PCIE_CONFIG_VGA_MMIO	(1 << 1)
#define   SCU_PCIE_CONFIG_VGA		(1 << 0)
#define SCU_PCIE_MMIO_CONFIG		0x184

/* LPC */
#define LPC_HICRB			0x100
#define   LPC_HICRB_ILPC_RO		(1 << 6)

struct ast_ahb_bridge_ops {
    int (*ilpc_status)(struct soc *soc, struct ast_cap_lpc *cap);
    int (*pci_status)(struct soc *soc, struct ast_cap_pci *cap);
    int (*debug_status)(struct soc *soc, struct ast_cap_uart *cap);
    int (*kernel_status)(struct soc *soc, struct ast_cap_kernel *cap);
    int (*xdma_status)(struct soc *soc, struct ast_cap_xdma *cap);
};

const char *ast_ip_state_desc[4] = {
    [ip_state_unknown] = "Unknown",
    [ip_state_absent] = "Absent",
    [ip_state_enabled] = "Enabled",
    [ip_state_disabled] = "Disabled",
};

static int region_readl(struct soc *soc, const struct soc_region *region,
                        uint32_t offset, uint32_t *val)
{
    return soc_readl(soc, region->start + offset, val);
}

static int region_writel(struct soc *soc, const struct soc_region *region,
                         uint32_t offset, uint32_t val)
{
    return soc_writel(soc, region->start + offset, val);
}

static int ast_ilpc_status(struct soc *soc, struct ast_cap_lpc *cap)
{
    static const struct soc_device_id scu_match[] = {
        { .compatible = "aspeed,ast2400-scu" },
        { .compatible = "aspeed,ast2500-scu" },
        { },
    };
    static const struct soc_device_id lpc_match[] = {
        { .compatible = "aspeed,ast2400-lpc-v2" },
        { .compatible = "aspeed,ast2500-lpc-v2" },
        { },
    };
    struct soc_device_node dn;
    struct soc_region scu, lpc;
    uint32_t val;
    int rc;

    /* Lookup the SCU mapping */
    if ((rc = soc_device_match_node(soc, scu_match, &dn)) < 0)
        return rc;

    if ((rc = soc_device_get_memory(soc, &dn, &scu)) < 0)
        return rc;

    /* Lookup the LPC mapping */
    if ((rc = soc_device_match_node(soc, lpc_match, &dn)) < 0)
        return rc;

    if ((rc = soc_device_get_memory(soc, &dn, &lpc)) < 0)
        return rc;

    /* Read the bridge state */
    if ((rc = region_readl(soc, &scu, SCU_HW_STRAP, &val)) < 0)
        return rc;

    cap->superio = !(val & SCU_HW_STRAP_SIO_DEC) ?
                        ip_state_enabled : ip_state_disabled;
    cap->ilpc.start = 0;
    cap->ilpc.len = (1ULL << 32);

    if ((rc = region_readl(soc, &lpc, LPC_HICRB, &val)) < 0)
        return rc;

    cap->ilpc.rw = !(val & LPC_HICRB_ILPC_RO);

    return 0;
}

static int ast2400_pci_status(struct soc *soc, struct ast_cap_pci *cap)
{
    static const struct soc_device_id scu_match[] = {
        { .compatible = "aspeed,ast2400-scu" },
        { },
    };
    struct soc_device_node dn;
    struct soc_region scu;
    struct ahb_range *r;
    uint32_t val;
    int rc;

    /* Lookup the SCU mapping */
    if ((rc = soc_device_match_node(soc, scu_match, &dn)) < 0)
        return rc;

    if ((rc = soc_device_get_memory(soc, &dn, &scu)) < 0)
        return rc;

    /* Read the bridge state */
    if ((rc = region_readl(soc, &scu, SCU_PCIE_CONFIG, &val)) < 0)
        return rc;

    cap->vga      = (val & SCU_PCIE_CONFIG_VGA) ?
                        ip_state_enabled : ip_state_disabled;
    cap->vga_mmio = (val & SCU_PCIE_CONFIG_VGA_MMIO) ?
                        ip_state_enabled : ip_state_disabled;
    cap->vga_xdma = (val & SCU_PCIE_CONFIG_VGA_XDMA) ?
                        ip_state_enabled : ip_state_disabled;
    cap->bmc      = (val & SCU_PCIE_CONFIG_BMC) ?
                        ip_state_enabled : ip_state_disabled;
    cap->bmc_mmio = (val & SCU_PCIE_CONFIG_BMC_MMIO) ?
                        ip_state_enabled : ip_state_disabled;
    cap->bmc_xdma = (val & SCU_PCIE_CONFIG_BMC_XDMA) ?
                        ip_state_enabled : ip_state_disabled;

    rc = region_readl(soc, &scu, SCU_MISC, &val);
    if (rc)
        return rc;

    r = &cap->ranges[p2ab_fw];
    r->name = "Firmware";
    r->start = 0;
    r->len = 0x18000000;
    r->rw = !(val & SCU_MISC_G4_P2A_FMC_RO);

    r = &cap->ranges[p2ab_soc];
    r->name = "SoC IO";
    r->start = 0x18000000;
    r->len = 0x08000000;
    r->rw = !(val & SCU_MISC_G4_P2A_SOC_RO);

    r = &cap->ranges[p2ab_fmc];
    r->name = "BMC Flash";
    r->start = 0x20000000;
    r->len = 0x10000000;
    r->rw = !(val & SCU_MISC_G4_P2A_FMC_RO);

    r = &cap->ranges[p2ab_spi];
    r->name = "Host Flash";
    r->start = 0x30000000;
    r->len = 0x10000000;
    r->rw = !(val & SCU_MISC_G4_P2A_SPI_RO);

    r = &cap->ranges[p2ab_dram];
    r->name = "DRAM";
    r->start = 0x40000000;
    r->len = 0x20000000;
    r->rw = !(val & SCU_MISC_G4_P2A_DRAM_RO);

    r = &cap->ranges[p2ab_lpch];
    r->name = "LPC Host";
    r->start = 0x60000000;
    r->len = 0x20000000;
    r->rw = !(val & SCU_MISC_G4_P2A_SOC_RO);

    r = &cap->ranges[p2ab_rsvd];
    r->name = "Reserved";
    r->start = 0x80000000;
    r->len = 0x80000000;
    r->rw = !(val & SCU_MISC_G4_P2A_SOC_RO);

    return 0;
}

static int ast2500_pci_status(struct soc *soc, struct ast_cap_pci *cap)
{
    static const struct soc_device_id scu_match[] = {
        { .compatible = "aspeed,ast2500-scu" },
        { },
    };
    struct soc_device_node dn;
    struct soc_region scu;
    struct ahb_range *r;
    uint32_t val;
    int rc;

    /* Lookup the SCU mapping */
    if ((rc = soc_device_match_node(soc, scu_match, &dn)) < 0)
        return rc;

    if ((rc = soc_device_get_memory(soc, &dn, &scu)) < 0)
        return rc;

    /* Read the bridge state */
    if ((rc = region_readl(soc, &scu, SCU_PCIE_CONFIG, &val)) < 0)
        return rc;

    cap->vga      = (val & SCU_PCIE_CONFIG_VGA) ?
                        ip_state_enabled : ip_state_disabled;
    cap->vga_mmio = (val & SCU_PCIE_CONFIG_VGA_MMIO) ?
                        ip_state_enabled : ip_state_disabled;
    cap->vga_xdma = (val & SCU_PCIE_CONFIG_VGA_XDMA) ?
                        ip_state_enabled : ip_state_disabled;
    cap->bmc      = (val & SCU_PCIE_CONFIG_BMC) ?
                        ip_state_enabled : ip_state_disabled;
    cap->bmc_mmio = (val & SCU_PCIE_CONFIG_BMC_MMIO) ?
                        ip_state_enabled : ip_state_disabled;
    cap->bmc_xdma = (val & SCU_PCIE_CONFIG_BMC_XDMA) ?
                        ip_state_enabled : ip_state_disabled;

    if ((rc = region_readl(soc, &scu, SCU_MISC, &val)) < 0)
        return rc;

    r = &cap->ranges[p2ab_fw];
    r->name = "Firmware";
    r->start = 0;
    r->len = 0x10000000;
    r->rw = !(val & SCU_MISC_G5_P2A_FLASH_RO);

    r = &cap->ranges[p2ab_soc];
    r->name = "SoC IO";
    r->start = 0x10000000;
    r->len = 0x10000000;
    r->rw = !(val & SCU_MISC_G5_P2A_SOC_RO);

    r = &cap->ranges[p2ab_fmc];
    r->name = "BMC Flash";
    r->start = 0x20000000;
    r->len = 0x10000000;
    r->rw = !(val & SCU_MISC_G5_P2A_FLASH_RO);

    r = &cap->ranges[p2ab_spi];
    r->name = "Host Flash";
    r->start = 0x30000000;
    r->len = 0x10000000;
    r->rw = !(val & SCU_MISC_G5_P2A_FLASH_RO);

    r = &cap->ranges[p2ab_rsvd];
    r->name = "Reserved";
    r->start = 0x40000000;
    r->len = 0x20000000;
    r->rw = !(val & SCU_MISC_G5_P2A_SOC_RO);

    r = &cap->ranges[p2ab_lpch];
    r->name = "LPC Host";
    r->start = 0x60000000;
    r->len = 0x20000000;
    r->rw = !(val & SCU_MISC_G5_P2A_LPCH_RO);

    r = &cap->ranges[p2ab_dram];
    r->name = "DRAM";
    r->start = 0x80000000;
    r->len = 0x80000000;
    r->rw = !(val & SCU_MISC_G5_P2A_DRAM_RO);

    return 0;
}

static int
ast2400_debug_status(struct soc *soc __unused, struct ast_cap_uart *cap)
{
    cap->debug = ip_state_absent;

    return 0;
}

static int ast2500_debug_status(struct soc *soc, struct ast_cap_uart *cap)
{
    static const struct soc_device_id scu_match[] = {
        { .compatible = "aspeed,ast2500-scu" },
        { },
    };
    struct soc_device_node dn;
    struct soc_region scu;
    uint32_t val;
    int rc;

    /* Lookup the SCU mapping */
    if ((rc = soc_device_match_node(soc, scu_match, &dn)) < 0)
        return rc;

    if ((rc = soc_device_get_memory(soc, &dn, &scu)) < 0)
        return rc;

    /* Read the bridge state */
    if ((rc = region_readl(soc, &scu, SCU_MISC, &val)) < 0)
        return rc;

    cap->debug = !(val & SCU_MISC_UART_DBG) ?
                    ip_state_enabled : ip_state_disabled;

    if ((rc = region_readl(soc, &scu, SCU_HW_STRAP, &val)) < 0)
        return rc;

    cap->uart = (val & SCU_HW_STRAP_UART_DBG_SEL) ? debug_uart5 : debug_uart1;

    return 0;
}

static int
ast_kernel_status(struct soc *soc __unused, struct ast_cap_kernel *cap)
{
    cap->have_devmem = (soc->ahb->bridge == ahb_devmem);

    return 0;
}

static int ast_xdma_status(struct soc *soc, struct ast_cap_xdma *cap)
{
    struct sdmc _sdmc, *sdmc = &_sdmc;
    int rc;

    if ((rc = sdmc_init(sdmc, soc)) < 0)
        return rc;

    if ((rc = sdmc_constrains_xdma(sdmc)) < 0)
        goto cleanup_sdmc;

    cap->unconstrained = !rc;
    if (rc == 1)
        rc = 0;

cleanup_sdmc:
    sdmc_destroy(sdmc);

    return rc;
}

int ast_ahb_bridge_discover(struct ahb *ahb, struct ast_interfaces *state)
{
    static const struct ast_ahb_bridge_ops ast2400_ops = {
        .ilpc_status = ast_ilpc_status,
        .pci_status = ast2400_pci_status,
        .debug_status = ast2400_debug_status,
        .kernel_status = ast_kernel_status,
        .xdma_status = ast_xdma_status,
    };
    static const struct ast_ahb_bridge_ops ast2500_ops = {
        .ilpc_status = ast_ilpc_status,
        .pci_status = ast2500_pci_status,
        .debug_status = ast2500_debug_status,
        .kernel_status = ast_kernel_status,
        .xdma_status = ast_xdma_status,
    };
    static const struct soc_device_id soc_match[] = {
        { .compatible = "aspeed,ast2400", .data = &ast2400_ops },
        { .compatible = "aspeed,ast2500", .data = &ast2500_ops },
        { },
    };
    const struct ast_ahb_bridge_ops *ops;
    struct soc _soc, *soc = &_soc;
    struct soc_device_node dn;
    int rc;

    if ((rc = soc_probe(soc, ahb)) < 0)
        goto cleanup_soc;

    if ((rc = soc_device_match_node(soc, soc_match, &dn)) < 0)
        goto cleanup_soc;

    if (!(ops = soc_device_get_match_data(soc, soc_match, &dn))) {
        rc = -ENOTSUP;
        goto cleanup_soc;
    }

    logi("Performing interface discovery via %s\n",
         ahb_interface_names[ahb->bridge]);

    if ((rc = ops->ilpc_status(soc, &state->lpc)) < 0)
        goto cleanup_soc;

    if ((rc = ops->pci_status(soc, &state->pci)) < 0)
        goto cleanup_soc;

    if ((rc = ops->debug_status(soc, &state->uart)) < 0)
        goto cleanup_soc;

    if ((rc = ops->kernel_status(soc, &state->kernel)) < 0)
        goto cleanup_soc;

    rc = ops->xdma_status(soc, &state->xdma);

cleanup_soc:
    soc_destroy(soc);

    return rc;
}

int ast_ahb_bridge_probe(struct ast_interfaces *state)
{
    struct devmem _devmem, *devmem = &_devmem;
    struct ilpcb _ilpcb, *ilpcb = &_ilpcb;
    struct p2ab _p2ab, *p2ab = &_p2ab;
    struct ahb _ahb, *ahb = &_ahb;
    int cleanup;
    int rc;

    if (!priv_am_root())
        return -EPERM;

    logi("Probing AHB interfaces\n");

    rc = devmem_init(devmem);
    if (!rc) {
        rc = devmem_probe(devmem);
        if (rc == 1) {
            ahb_use(ahb, ahb_devmem, devmem);

            rc = ast_ahb_bridge_discover(ahb, state);

            cleanup = devmem_destroy(devmem);
            if (cleanup) { errno = -cleanup; perror("devmem_destroy"); }

            return rc;
        }

        cleanup = devmem_destroy(devmem);
        if (cleanup) { errno = -cleanup; perror("devmem_destroy"); }
    }

    rc = p2ab_init(p2ab, AST_PCI_VID, AST_PCI_DID_VGA);
    if (!rc) {
        rc = p2ab_probe(p2ab);
        if (rc == 1) {
            ahb_use(ahb, ahb_p2ab, p2ab);

            rc = ast_ahb_bridge_discover(ahb, state);

            cleanup = p2ab_destroy(p2ab);
            if (cleanup) { errno = -cleanup; perror("p2ab_destroy"); }

            return rc;
        }

        cleanup = p2ab_destroy(p2ab);
        if (cleanup) { errno = -cleanup; perror("p2ab_destroy"); }
    }

    rc = ilpcb_init(ilpcb);
    if (!rc) {
        rc = ilpcb_probe(ilpcb);
        if (rc == 1) {
            ahb_use(ahb, ahb_ilpcb, ilpcb);

            rc = ast_ahb_bridge_discover(ahb, state);

            cleanup = ilpcb_destroy(ilpcb);
            if (cleanup) { errno = -cleanup; perror("ilpcb_destroy"); }

            return rc;
        }
        cleanup = ilpcb_destroy(ilpcb);
        if (cleanup) { errno = -cleanup; perror("ilpcb_destroy"); }
    }

    return -ENOTSUP;
}

static int ast_p2ab_enable_writes(struct soc *soc, const struct soc_region *scu)
{
    uint32_t val;
    int rc;

    logi("Disabling %s write filters\n", ahb_interface_names[ahb_p2ab]);
    if ((rc = region_readl(soc, scu, SCU_MISC, &val)) < 0)
        return rc;

    /* Unconditionally turn off all write filters */
    if (soc_generation(soc) == ast_g4) {
        val &= ~(SCU_MISC_G4_P2A_DRAM_RO | SCU_MISC_G4_P2A_SPI_RO |
                SCU_MISC_G4_P2A_SOC_RO  | SCU_MISC_G4_P2A_FMC_RO);
    } else if (soc_generation(soc) == ast_g5) {
        val &= ~(SCU_MISC_G5_P2A_DRAM_RO | SCU_MISC_G5_P2A_LPCH_RO |
                SCU_MISC_G5_P2A_SOC_RO  | SCU_MISC_G5_P2A_FLASH_RO);
    } else if (soc_generation(soc) == ast_g6) {
        return 0;
    } else {
        return -ENOTSUP;
    }

    if ((rc = region_writel(soc, scu, SCU_MISC, val)) < 0)
        return rc;

    return 0;
}

int ast_ahb_init(struct ahb *ahb, bool rw)
{
    bool have_vga, have_vga_mmio, have_bmc, have_bmc_mmio, have_p2ab;
    static const struct soc_device_id scu_match[] = {
        { .compatible = "aspeed,ast2400-scu" },
        { .compatible = "aspeed,ast2500-scu" },
        { },
    };
    static const struct soc_device_id lpc_match[] = {
        { .compatible = "aspeed,ast2400-lpc-v2" },
        { .compatible = "aspeed,ast2500-lpc-v2" },
        { },
    };
    struct p2ab _p2ab, *p2ab = &_p2ab;
    struct soc _soc, *soc = &_soc;
    struct ast_interfaces state;
    struct soc_region scu, lpc;
    struct soc_device_node dn;
    uint32_t val;
    int rc;

    rc = ast_ahb_bridge_probe(&state);
    if (rc)
        return rc;

    if (state.kernel.have_devmem) {
        logi("Detected devmem interface\n");
        return ahb_init(ahb, ahb_devmem);
    }

    have_vga = state.pci.vga == ip_state_enabled;
    have_vga_mmio = state.pci.vga_mmio == ip_state_enabled;
    have_bmc = state.pci.bmc == ip_state_enabled;
    have_bmc_mmio = state.pci.bmc_mmio == ip_state_enabled;
    have_p2ab = (have_vga && have_vga_mmio) || (have_bmc && have_bmc_mmio);

    if (!have_p2ab || (rw && !state.pci.ranges[p2ab_soc].rw)) {
        if (state.lpc.superio != ip_state_enabled)
            return -ENOTSUP;

        if (!state.lpc.ilpc.rw && rw)
            return -ENOTSUP;

        rc = ahb_init(ahb, ahb_ilpcb);
        if (rc || !rw)
            goto cleanup;

        if ((rc = soc_probe(soc, ahb)) < 0)
            goto cleanup_ilpc_ahb;

        if (!have_p2ab) {
            if ((rc = soc_device_match_node(soc, scu_match, &dn)) < 0)
                goto cleanup_ilpc_soc;

            if ((rc = soc_device_get_memory(soc, &dn, &scu)) < 0)
                goto cleanup_ilpc_soc;

            /* Enable the P2A interface */
            logi("Enabling %s interface via %s\n",
                 ahb_interface_names[ahb_p2ab], ahb_interface_names[ahb_ilpcb]);

            if ((rc = region_readl(soc, &scu, SCU_PCIE_CONFIG, &val)) < 0)
                goto cleanup_ilpc_soc;

            val |= (SCU_PCIE_CONFIG_VGA | SCU_PCIE_CONFIG_VGA_MMIO);

            if ((rc = region_writel(soc, &scu, SCU_PCIE_CONFIG, val)) < 0)
                goto cleanup_ilpc_soc;

            rc = system("echo 1 > /sys/bus/pci/rescan");
            if (rc == -1) {
                rc = -errno;
                goto cleanup_ilpc_soc;
            } else if (rc == 127) {
                rc = -EIO;
                goto cleanup_ilpc_soc;
            }

            usleep(1000);
        }

        /* Make the P2A interface writable */
        if (rw && !state.pci.ranges[p2ab_soc].rw) {
            if ((rc = ast_p2ab_enable_writes(soc, &scu)) < 0)
                goto cleanup_ilpc_soc;
        }

cleanup_ilpc_soc:
        soc_destroy(soc);

cleanup_ilpc_ahb:
        ahb_destroy(ahb);
    }

    if ((rc = p2ab_init(p2ab, AST_PCI_VID, AST_PCI_DID_VGA)) < 0)
        return rc;

    if (p2ab_probe(p2ab) < 1) {
        p2ab_destroy(p2ab);
        if ((rc = ahb_init(ahb, ahb_l2ab)) < 0)
            goto cleanup;
        goto done;
    }

    if ((rc = ahb_init(ahb, ahb_p2ab)) < 0)
        goto cleanup;

    if (state.lpc.superio != ip_state_enabled) {
        if ((rc = soc_probe(soc, ahb)) < 0)
            goto cleanup_p2a_ahb;

        if ((rc = soc_device_match_node(soc, scu_match, &dn)) < 0)
            goto cleanup_p2a_soc;

        if ((rc = soc_device_get_memory(soc, &dn, &scu)) < 0)
            goto cleanup_p2a_soc;

        /* Enable the LPC interface */
        logi("Enabling %s interface via %s\n",
             ahb_interface_names[ahb_ilpcb], ahb_interface_names[ahb_p2ab]);

        val = SCU_HW_STRAP_SIO_DEC;
        if ((rc = region_writel(soc, &scu, SCU_SILICON_REVISION, val)) < 0)
            goto cleanup_p2a_soc;

        /* Make the LPC interface writable */
        if (rw && !state.lpc.ilpc.rw) {
            if ((rc = soc_device_match_node(soc, lpc_match, &dn)) < 0)
                goto cleanup_p2a_soc;

            if ((rc = soc_device_get_memory(soc, &dn, &lpc)) < 0)
                goto cleanup_p2a_soc;

            if ((rc = region_readl(soc, &lpc, LPC_HICRB, &val)) < 0)
                goto cleanup_p2a_soc;

            val &= ~LPC_HICRB_ILPC_RO;

            if ((rc = region_writel(soc, &lpc, LPC_HICRB, val)) < 0)
                goto cleanup_p2a_soc;
        }

cleanup_p2a_soc:
        soc_destroy(soc);
    }

    if (rc) {
cleanup_p2a_ahb:
        ahb_destroy(ahb);
cleanup:
        loge("Probed initialisation failed: %d\n", rc);
        return rc;
    }

done:
    logi("Probed initialisation complete, using %s interface\n",
         ahb_interface_names[ahb->bridge]);

    return 0;
}

int ast_ahb_from_args(struct ahb *ahb, int argc, char *argv[])
{
    /* Local interfaces */
    if (argc == 0) {
        logi("Probing local interfaces\n");
        return ast_ahb_init(ahb, true);
    }

    /* Local debug interface */
    if (argc == 1)
        return ahb_init(ahb, ahb_debug, argv[0]);

    /* Remote debug interface */
    assert(argc == 5);
    return ahb_init(ahb, ahb_debug, argv[0], argv[1], strtoul(argv[2], NULL, 0),
                    argv[3], argv[4]);
}

int ast_ahb_access(const char *name __unused, int argc, char *argv[],
                   struct ahb *ahb)
{
    uint32_t address, data;
    bool action_read;
    int rc;

    if (argc < 2) {
        loge("Not enough arguments for AHB access\n");
        exit(EXIT_FAILURE);
    }

    if (!strcmp("read", argv[0]))
        action_read = true;
    else if (!strcmp("write", argv[0]))
        action_read = false;
    else {
        loge("Unknown action: %s\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    address = strtoul(argv[1], NULL, 0);

    if (!action_read) {
        if (argc < 3) {
            loge("Not enough arguments for AHB write command\n");
            exit(EXIT_FAILURE);
        }
        data = strtoul(argv[2], NULL, 0);
    }

    if (action_read) {
        rc = ahb_readl(ahb, address, &data);
        if (rc) {
            errno = -rc;
            perror("ahb_readl");
            exit(EXIT_FAILURE);
        }
        printf("0x%08x: 0x%08x\n", address, le32toh(data));
    } else {
        rc = ahb_writel(ahb, address, htole32(data));
        if (rc) {
            errno = -rc;
            perror("ahb_writel");
            exit(EXIT_FAILURE);
        }
    }

    return 0;
}
