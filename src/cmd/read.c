// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

#include "ahb.h"
#include "ast.h"
#include "compiler.h"
#include "flash.h"
#include "host.h"
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
    struct soc_region dram, vram;
    struct sdmc *sdmc;
    int rc;

    if (!(sdmc = sdmc_get(soc))) {
        return -ENODEV;
    }

    if ((rc = sdmc_get_dram(sdmc, &dram))) {
        return rc;
    }

    if ((rc = sdmc_get_vram(sdmc, &vram))) {
        return rc;
    }

    logi("%dMiB DRAM with %dMiB VRAM; dumping %dMiB (0x%x-0x%08x)\n",
         dram.length >> 20, vram.length >> 20,
         (dram.length - vram.length) >> 20, dram.start, vram.start - 1);

    rc = soc_siphon_in(soc, dram.start, dram.length - vram.length, 1);
    if (rc) {
        errno = -rc;
        perror("soc_siphon_in");
    }

    return rc;
}

int cmd_read(const char *name __unused, int argc, char *argv[])
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct ahb *ahb;
    int rc;

    if (argc < 1) {
        loge("Not enough arguments for read command\n");
        exit(EXIT_FAILURE);
    }

    if ((rc = host_init(host, argc - 1, argv + 1)) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        exit(EXIT_FAILURE);
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = EXIT_FAILURE;
        goto cleanup_host;
    }

    if ((rc = soc_probe(soc, ahb)) < 0)
        goto cleanup_host;

    if (!strcmp("firmware", argv[0]))
        rc = cmd_dump_firmware(soc);
    else if (!strcmp("ram", argv[0]))
        rc = cmd_dump_ram(soc);
    else {
        loge("Unsupported read type '%s'", argv[0]);
        rc = -EINVAL;
    }

    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    if (rc < 0)
        exit(EXIT_FAILURE);

    return 0;
}
