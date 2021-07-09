// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

#include "ahb.h"
#include "ast.h"
#include "flash.h"
#include "log.h"
#include "priv.h"
#include "sdmc.h"
#include "sfc.h"
#include "soc.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmd_dump_firmware(struct soc *soc)
{
    struct soc_region flash;
    struct flash_chip *chip;
    struct sfc *sfc;
    uint32_t wp;
    int cleanup;
    int rc;

    logi("Initialising flash controller\n");
    if ((rc = sfc_init(&sfc, soc, "fmc")))
        return rc;

    logi("Initialising flash chip\n");
    if ((rc = flash_init(sfc, &chip)))
        goto cleanup_sfc;

    logi("Write-protecting all chip-selects\n");
    if ((rc = sfc_write_protect_save(sfc, true, &wp)))
        goto cleanup_chip;

    if ((rc = sfc_get_flash(sfc, &flash)) < 0)
        goto cleanup_chip;

    logi("Exfiltrating BMC flash to stdout\n\n");
    rc = soc_siphon_in(soc, flash.start, chip->info.size, 1);
    if (rc) { errno = -rc; perror("soc_siphon_in"); }

    if ((cleanup = sfc_write_protect_restore(sfc, wp)) < 0) {
        errno = -rc;
        perror("sfc_write_protect_restore");
    }

cleanup_chip:
    flash_destroy(chip);

cleanup_sfc:
    if ((cleanup = sfc_destroy(sfc)) < 0) {
        errno = -cleanup;
        perror("sfc_destroy");
    }

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

    if ((rc = soc_probe(soc, ahb)) < 0)
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
