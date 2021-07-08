// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

#include "ahb.h"
#include "ast.h"
#include "log.h"
#include "priv.h"
#include "sdmc.h"
#include "soc.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BMC_FLASH_LEN   (32 << 20)

static int cmd_dump_firmware(struct soc *soc)
{
    uint32_t restore_tsr, sfc_tsr, sfc_wafcr;
    int cleanup;
    int rc;

    logi("Testing BMC SFC write filter configuration\n");
    rc = soc_readl(soc, 0x1e6200a4, &sfc_wafcr);
    if (rc) { errno = -rc; perror("soc_readl"); return rc; }

    if (sfc_wafcr) {
        loge("Found write filter configuration 0x%x\n", sfc_wafcr);
        loge("BMC has selective write filtering enabled, bailing!\n");
        return -ENOTSUP;
    }

    /* Disable writes to CE0 - chip enables are swapped for alt boot */
    logi("Write-protecting BMC SFC\n");
    rc = soc_readl(soc, 0x1e620000, &sfc_tsr);
    if (rc) { errno = -rc; perror("soc_readl"); return rc; }

    restore_tsr = sfc_tsr;
    sfc_tsr &= ~(1 << 16);

    rc = soc_writel(soc, 0x1e620000, sfc_tsr);
    if (rc) { errno = -rc; perror("soc_writel"); goto cleanup_sfc; }

    logi("Exfiltrating BMC flash to stdout\n\n");
    rc = soc_siphon_in(soc, AST_G5_BMC_FLASH, BMC_FLASH_LEN, 1);
    if (rc) { errno = -rc; perror("soc_siphon_in"); }

cleanup_sfc:
    logi("Clearing BMC SFC write protect state\n");
    cleanup = soc_writel(soc, 0x1e620000, restore_tsr);
    if (cleanup) { errno = -cleanup; perror("soc_writel"); }

    return rc;
}

static int cmd_dump_ram(struct soc *soc)
{
    struct sdmc _sdmc, *sdmc = &_sdmc;
    struct soc_region dram, vram;
    int rc;

    if ((rc = sdmc_init(sdmc, soc)))
        return rc;

    if ((rc = sdmc_get_dram(sdmc, &dram)))
        return rc;

    if ((rc = sdmc_get_vram(sdmc, &vram)))
        return rc;

    logi("%dMiB DRAM with %dMiB VRAM; dumping %dMiB (0x%x-0x%08x)\n",
         dram.length >> 20, vram.length >> 20,
         (dram.length - vram.length) >> 20, dram.start, vram.start - 1);

    rc = soc_siphon_in(soc, dram.start, dram.length - vram.length, 1);
    if (rc) {
        errno = -rc;
        perror("soc_siphon_in");
    }

    sdmc_destroy(sdmc);

    return rc;
}

int cmd_read(const char *name, int argc, char *argv[])
{
    struct soc _soc, *soc = &_soc;
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

    rc = soc_probe(soc, ahb);
    if (rc < 0)
        goto cleanup_ahb;

    if (!strcmp("firmware", argv[0]))
        rc = cmd_dump_firmware(soc);
    else if (!strcmp("ram", argv[0]))
        rc = cmd_dump_ram(soc);
    else {
        loge("Unsupported read type '%s'", argv[0]);
        rc = -EINVAL;
    }

    soc_destroy(soc);

cleanup_ahb:
    cleanup = ahb_destroy(ahb);
    if (cleanup) { errno = -cleanup; perror("ahb_destroy"); }

    if (rc < 0)
        exit(EXIT_FAILURE);

    return 0;
}
