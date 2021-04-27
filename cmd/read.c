// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ahb.h"
#include "ast.h"
#include "log.h"
#include "priv.h"

#define BMC_FLASH_LEN   (32 << 20)

static int cmd_dump_firmware(struct ahb *ahb)
{
    uint32_t restore_tsr, sfc_tsr, sfc_wafcr;
    int cleanup;
    int rc;

    logi("Testing BMC SFC write filter configuration\n");
    rc = ahb_readl(ahb, 0x1e6200a4, &sfc_wafcr);
    if (rc) { errno = -rc; perror("ahb_readl"); return rc; }

    if (sfc_wafcr) {
        loge("Found write filter configuration 0x%x\n", sfc_wafcr);
        loge("BMC has selective write filtering enabled, bailing!\n");
        return -ENOTSUP;
    }

    /* Disable writes to CE0 - chip enables are swapped for alt boot */
    logi("Write-protecting BMC SFC\n");
    rc = ahb_readl(ahb, 0x1e620000, &sfc_tsr);
    if (rc) { errno = -rc; perror("ahb_readl"); return rc; }

    restore_tsr = sfc_tsr;
    sfc_tsr &= ~(1 << 16);

    rc = ahb_writel(ahb, 0x1e620000, sfc_tsr);
    if (rc) { errno = -rc; perror("ahb_writel"); goto cleanup_sfc; }

    logi("Exfiltrating BMC flash to stdout\n\n");
    rc = ahb_siphon_in(ahb, AST_G5_BMC_FLASH, BMC_FLASH_LEN, 1);
    if (rc) { errno = -rc; perror("ahb_siphon_in"); }

cleanup_sfc:
    logi("Clearing BMC SFC write protect state\n");
    cleanup = ahb_writel(ahb, 0x1e620000, restore_tsr);
    if (cleanup) { errno = -cleanup; perror("ahb_writel"); }

    return rc;
}

static int cmd_dump_ram(struct ahb *ahb)
{
    uint32_t scu_rev, sdmc_conf;
    uint32_t dram, vram, aram;
    int rc;

    /* Test BMC silicon revision to make sure we use the right memory map */
    logi("Checking ASPEED BMC silicon revision\n");
    rc = ahb_readl(ahb, 0x1e6e207c, &scu_rev);
    if (rc) { errno = -rc; perror("ahb_readl"); return rc; }

    if ((scu_rev >> 24) != 0x04) {
        loge("Unsupported BMC revision: 0x%08x\n", scu_rev);
        return -ENOTSUP;
    }

    logi("Found AST2500-family BMC\n");

    rc = ahb_readl(ahb, 0x1e6e0004, &sdmc_conf);
    if (rc) { errno = -rc; perror("ahb_readl"); return rc; }

    dram = bmc_dram_sizes[sdmc_conf & 0x03];
    vram = bmc_vram_sizes[(sdmc_conf >> 2) & 0x03];
    aram = dram - vram; /* Accessible DRAM */

    logi("%dMiB DRAM with %dMiB VRAM; dumping %dMiB (0x%x-0x%08x)\n",
         dram >> 20, vram >> 20, aram >> 20, AST_G5_DRAM,
         AST_G5_DRAM + aram - 1);

    rc = ahb_siphon_in(ahb, AST_G5_DRAM, aram, 1);
    if (rc) { errno = -rc; perror("ahb_siphon_in"); }

    return rc;
}

int cmd_read(const char *name, int argc, char *argv[])
{
    struct ahb _ahb, *ahb = &_ahb;
    int rc, cleanup;

    if (argc < 1) {
        loge("Not enough arguments for read command\n");
        exit(EXIT_FAILURE);
    }

    rc = ast_ahb_from_args(ahb, argc - 1, argv + 1);
    printf("ast_ahb_from_args: 0x%x\n", rc);
    if (rc < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(name);
        } else if (rc == -ENOTSUP) {
            loge("Probes failed, cannot access BMC AHB\n");
        } else {
            errno = -rc;
            perror("ast_ahb_from_args");
        }
        exit(EXIT_FAILURE);
    }

    if (!strcmp("firmware", argv[0]))
        rc = cmd_dump_firmware(ahb);
    else if (!strcmp("ram", argv[0]))
        rc = cmd_dump_ram(ahb);
    else {
        loge("Unsupported read type '%s'", argv[0]);
        rc = -EINVAL;
    }

    cleanup = ahb_destroy(ahb);
    if (cleanup) { errno = -cleanup; perror("ahb_destroy"); }

    if (rc < 0)
        exit(EXIT_FAILURE);

    return 0;
}
