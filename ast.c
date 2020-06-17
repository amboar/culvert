// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ast.h"
#include "devmem.h"
#include "ilpc.h"
#include "log.h"
#include "mb.h"
#include "p2a.h"
#include "priv.h"
#include "rev.h"

const uint32_t bmc_dram_sizes[4] = {
    [0b00] = 128  << 20,
    [0b01] = 256  << 20,
    [0b10] = 512  << 20,
    [0b11] = 1024 << 20,
};

const uint32_t bmc_vram_sizes[4] = {
    [0b00] = 8  << 20,
    [0b01] = 16 << 20,
    [0b10] = 32 << 20,
    [0b11] = 64 << 20,
};

const char *ast_ip_state_desc[4] = {
    [ip_state_unknown] = "Unknown",
    [ip_state_absent] = "Absent",
    [ip_state_enabled] = "Enabled",
    [ip_state_disabled] = "Disabled",
};

static int ast_ilpcb_status(struct ahb *ctx, struct ast_cap_lpc *lpc)
{
    uint32_t val;
    int rc;

    rc = ahb_readl(ctx, AST_G5_SCU | SCU_HW_STRAP, &val);
    if (rc)
        return rc;

    lpc->superio = !(val & SCU_HW_STRAP_SIO_DEC) ?
                        ip_state_enabled : ip_state_disabled;
    lpc->ilpc.start = 0;
    lpc->ilpc.len = (1ULL << 32);

    rc = ahb_readl(ctx, AST_G5_LPC | LPC_HICRB, &val);
    if (rc)
        return rc;

    lpc->ilpc.rw = !(val & LPC_HICRB_ILPC_RO);

    return 0;
}

static int ast_pci_status(struct ahb *ctx, struct ast_cap_pci *pci)
{
    struct ahb_range *r;
    uint32_t val;
    uint32_t rev;
    int rc;

    rc = ahb_readl(ctx, AST_G5_SCU | SCU_PCIE_CONFIG, &val);
    if (rc)
        return rc;

    pci->vga      = (val & SCU_PCIE_CONFIG_VGA) ?
                        ip_state_enabled : ip_state_disabled;
    pci->vga_mmio = (val & SCU_PCIE_CONFIG_VGA_MMIO) ?
                        ip_state_enabled : ip_state_disabled;
    pci->vga_xdma = (val & SCU_PCIE_CONFIG_VGA_XDMA) ?
                        ip_state_enabled : ip_state_disabled;
    pci->bmc      = (val & SCU_PCIE_CONFIG_BMC) ?
                        ip_state_enabled : ip_state_disabled;
    pci->bmc_mmio = (val & SCU_PCIE_CONFIG_BMC_MMIO) ?
                        ip_state_enabled : ip_state_disabled;
    pci->bmc_xdma = (val & SCU_PCIE_CONFIG_BMC_XDMA) ?
                        ip_state_enabled : ip_state_disabled;

    rc = ahb_readl(ctx, AST_G5_SCU | SCU_MISC, &val);
    if (rc)
        return rc;

    rev = rev_probe(ctx);
    if (rev < 0)
        return rev;

    if (rev_is_generation(rev, ast_g4)) {
        r = &pci->ranges[p2ab_fw];
        r->name = "Firmware";
        r->start = 0;
        r->len = 0x18000000;
        r->rw = !(val & SCU_MISC_G4_P2A_FMC_RO);

        r = &pci->ranges[p2ab_soc];
        r->name = "SoC IO";
        r->start = 0x18000000;
        r->len = 0x08000000;
        r->rw = !(val & SCU_MISC_G4_P2A_SOC_RO);

        r = &pci->ranges[p2ab_fmc];
        r->name = "BMC Flash";
        r->start = 0x20000000;
        r->len = 0x10000000;
        r->rw = !(val & SCU_MISC_G4_P2A_FMC_RO);

        r = &pci->ranges[p2ab_spi];
        r->name = "Host Flash";
        r->start = 0x30000000;
        r->len = 0x10000000;
        r->rw = !(val & SCU_MISC_G4_P2A_SPI_RO);

        r = &pci->ranges[p2ab_dram];
        r->name = "DRAM";
        r->start = 0x40000000;
        r->len = 0x20000000;
        r->rw = !(val & SCU_MISC_G4_P2A_DRAM_RO);

        r = &pci->ranges[p2ab_lpch];
        r->name = "LPC Host";
        r->start = 0x60000000;
        r->len = 0x20000000;
        r->rw = !(val & SCU_MISC_G4_P2A_SOC_RO);

        r = &pci->ranges[p2ab_rsvd];
        r->name = "Reserved";
        r->start = 0x80000000;
        r->len = 0x80000000;
        r->rw = !(val & SCU_MISC_G4_P2A_SOC_RO);
    } else if (rev_is_generation(rev, ast_g5)) {
        r = &pci->ranges[p2ab_fw];
        r->name = "Firmware";
        r->start = 0;
        r->len = 0x10000000;
        r->rw = !(val & SCU_MISC_G5_P2A_FLASH_RO);

        r = &pci->ranges[p2ab_soc];
        r->name = "SoC IO";
        r->start = 0x10000000;
        r->len = 0x10000000;
        r->rw = !(val & SCU_MISC_G5_P2A_SOC_RO);

        r = &pci->ranges[p2ab_fmc];
        r->name = "BMC Flash";
        r->start = 0x20000000;
        r->len = 0x10000000;
        r->rw = !(val & SCU_MISC_G5_P2A_FLASH_RO);

        r = &pci->ranges[p2ab_spi];
        r->name = "Host Flash";
        r->start = 0x30000000;
        r->len = 0x10000000;
        r->rw = !(val & SCU_MISC_G5_P2A_FLASH_RO);

        r = &pci->ranges[p2ab_rsvd];
        r->name = "Reserved";
        r->start = 0x40000000;
        r->len = 0x20000000;
        r->rw = !(val & SCU_MISC_G5_P2A_SOC_RO);

        r = &pci->ranges[p2ab_lpch];
        r->name = "LPC Host";
        r->start = 0x60000000;
        r->len = 0x20000000;
        r->rw = !(val & SCU_MISC_G5_P2A_LPCH_RO);

        r = &pci->ranges[p2ab_dram];
        r->name = "DRAM";
        r->start = 0x80000000;
        r->len = 0x80000000;
        r->rw = !(val & SCU_MISC_G5_P2A_DRAM_RO);
    } else {
        loge("No description for the PCIe configuration layout on the %s\n",
             rev_name(rev));
    }

    return 0;
}

static int ast_debug_status(struct ahb *ctx, struct ast_cap_uart *uart)
{
    uint32_t val;
    uint32_t rev;
    int rc;

    rev = rev_probe(ctx);
    if (rev < 0)
        return rev;

    if (rev_is_generation(rev, ast_g6))
        return 0;

    if (rev_is_generation(rev, ast_g4)) {
        uart->debug = ip_state_absent;
        return 0;
    }

    rc = ahb_readl(ctx, AST_G5_SCU | SCU_MISC, &val);
    if (rc)
        return rc;

    uart->debug = !(val & SCU_MISC_UART_DBG) ?
                    ip_state_enabled : ip_state_disabled;

    rc = ahb_readl(ctx, AST_G5_SCU | SCU_HW_STRAP, &val);
    if (rc)
        return rc;

    uart->uart = (val & SCU_HW_STRAP_UART_DBG_SEL) ? debug_uart5 : debug_uart1;

    return 0;
}

static int ast_kernel_status(struct ahb *ctx, struct ast_cap_kernel *kernel)
{
    kernel->have_devmem = (ctx->bridge == ahb_devmem);

    return 0;
}

static int ast_xdma_status(struct ahb *ctx, struct ast_cap_xdma *xdma)
{
    uint32_t val;
    uint32_t rev;
    int rc;

    rev = rev_probe(ctx);
    if (rev < 0)
        return rev;

    if (rev_is_generation(rev, ast_g6))
        return 0;

    rc = ahb_readl(ctx, AST_G5_SDMC | SDMC_GMP, &val);
    if (rc)
        return rc;

    if (rev_is_generation(rev, ast_g4)) {
        xdma->unconstrained = !(val & SDMC_GMP_G4_XDMA);
    } else if (rev_is_generation(rev, ast_g5)) {
        xdma->unconstrained = !(val & SDMC_GMP_G5_XDMA);
    } else {
        return -ENOTSUP;
    }

    return 0;
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

int ast_ahb_bridge_discover(struct ahb *ahb, struct ast_interfaces *state)
{
    const char *chip;
    int64_t val;
    int rc;

    logi("Performing interface discovery via %s\n",
         ahb_interface_names[ahb->bridge]);

    val = rev_probe(ahb);
    if (val < 0)
        return val;

    chip = rev_name(val);
    assert(chip);
    logi("Detected %s\n", chip);

    rc = ast_ilpcb_status(ahb, &state->lpc);
    if (rc)
        return rc;

    rc = ast_pci_status(ahb, &state->pci);
    if (rc)
        return rc;

    rc = ast_debug_status(ahb, &state->uart);
    if (rc)
        return rc;

    rc = ast_kernel_status(ahb, &state->kernel);
    if (rc)
        return rc;

    return ast_xdma_status(ahb, &state->xdma);
}

static int ast_p2ab_enable_writes(struct ahb *ahb)
{
    uint32_t val;
    uint32_t rev;
    int rc;

    logi("Disabling %s write filters\n", ahb_interface_names[ahb_p2ab]);

    rc = ahb_readl(ahb, AST_G5_SCU | SCU_MISC, &val);
    if (rc)
        return rc;

    rev = rev_probe(ahb);
    if (rc < 0)
        return rc;

    /* Unconditionally turn off all write filters */
    if (rev_is_generation(rev, ast_g4)) {
        val &= ~(SCU_MISC_G4_P2A_DRAM_RO | SCU_MISC_G4_P2A_SPI_RO |
                SCU_MISC_G4_P2A_SOC_RO  | SCU_MISC_G4_P2A_FMC_RO);
    } else if (rev_is_generation(rev, ast_g5)) {
        val &= ~(SCU_MISC_G5_P2A_DRAM_RO | SCU_MISC_G5_P2A_LPCH_RO |
                SCU_MISC_G5_P2A_SOC_RO  | SCU_MISC_G5_P2A_FLASH_RO);
    } else if (rev_is_generation(rev, ast_g6)) {
        return 0;
    } else {
        return -ENOTSUP;
    }

    rc = ahb_writel(ahb, AST_G5_SCU | SCU_MISC, val);

    return rc;
}

int ast_ahb_init(struct ahb *ahb, bool rw)
{
    bool have_vga, have_vga_mmio, have_bmc, have_bmc_mmio, have_p2ab;
    bool enabled_rw_p2ab;
    struct ast_interfaces state;
    uint32_t val;
    int rc;

    enabled_rw_p2ab = false;

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

        if (!have_p2ab) {
            logi("Enabling %s interface via %s\n",
                 ahb_interface_names[ahb_p2ab], ahb_interface_names[ahb_ilpcb]);
            rc = ahb_readl(ahb, AST_G5_SCU | SCU_PCIE_CONFIG, &val);
            if (rc)
                goto cleanup_ahb;

            val |= (SCU_PCIE_CONFIG_VGA | SCU_PCIE_CONFIG_VGA_MMIO);

            rc = ahb_writel(ahb, AST_G5_SCU | SCU_PCIE_CONFIG, val);
            if (rc)
                goto cleanup_ahb;

            rc = system("echo 1 > /sys/bus/pci/rescan");
            if (rc == -1) {
                rc = -errno;
                goto cleanup_ahb;
            } else if (rc == 127) {
                rc = -EIO;
                goto cleanup_ahb;
            }

            usleep(1000);
        }

        if (rw && !state.pci.ranges[p2ab_soc].rw) {
            rc = ast_p2ab_enable_writes(ahb);
            if (rc)
                goto cleanup_ahb;

            enabled_rw_p2ab = true;
        }

        ahb_destroy(ahb);
    }

    struct p2ab _p2ab, *p2ab = &_p2ab;

    rc = p2ab_init(p2ab, AST_PCI_VID, AST_PCI_DID_VGA);
    if (rc)
        return rc;

    if (p2ab_probe(p2ab) < 1) {
        p2ab_destroy(p2ab);
        rc = ahb_init(ahb, ahb_l2ab);
        if (rc < 0)
            goto cleanup;
        goto done;
    }

    rc = ahb_init(ahb, ahb_p2ab);
    if (rc)
        goto cleanup;

    if (!enabled_rw_p2ab) {
        rc = ast_p2ab_enable_writes(ahb);
        if (rc)
            goto cleanup_ahb;
    }

    if (state.lpc.superio != ip_state_enabled) {
        logi("Enabling %s interface via %s\n",
             ahb_interface_names[ahb_ilpcb], ahb_interface_names[ahb_p2ab]);
        val = rev_probe(ahb);
        if (val < 0) { rc = val; goto cleanup_ahb; }

        if (rev_is_generation(val, ast_g6)) { rc = 0; goto cleanup_ahb; }

        val = SCU_HW_STRAP_SIO_DEC;

        rc = ahb_writel(ahb, AST_G5_SCU | SCU_SILICON_REVISION, val);
        if (rc)
            goto cleanup_ahb;

        if (rw && !state.lpc.ilpc.rw) {
            rc = ahb_readl(ahb, AST_G5_LPC | LPC_HICRB, &val);
            if (rc)
                goto cleanup_ahb;

            val &= ~LPC_HICRB_ILPC_RO;

            rc = ahb_writel(ahb, AST_G5_LPC | LPC_HICRB, val);
            if (rc)
                goto cleanup_ahb;
        }
    }

done:
    logi("Probed initialisation complete, using %s interface\n",
         ahb_interface_names[ahb->bridge]);

    return 0;

cleanup_ahb:
    ahb_destroy(ahb);

cleanup:
    loge("Probed initialisation failed: %d\n", rc);

    return rc;
}
