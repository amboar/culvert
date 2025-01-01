// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

#include "ahb.h"
#include "ast.h"
#include "cmd.h"
#include "compiler.h"
#include "flash.h"
#include "host.h"
#include "log.h"
#include "priv.h"
#include "soc.h"
#include "soc/sdmc.h"
#include "soc/sfc.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int cmd_read_firmware(int argc, char *argv[])
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct ahb *ahb;
    struct soc_region flash;
    struct flash_chip *chip;
    struct sfc *sfc;
    uint32_t wp;
    int cleanup;
    int rc;

    if ((rc = host_init(host, argc, argv)) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        return rc;
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = -ENODEV;
        goto cleanup_host;
    }

    if ((rc = soc_probe(soc, ahb)) < 0)
        goto cleanup_host;

    logi("Initialising flash controller\n");
    if (!(sfc = sfc_get_by_name(soc, "fmc"))) {
        loge("Failed to acquire SPI controller\n");
        rc = -ENODEV;
        goto cleanup_soc;
    }

    logi("Initialising flash chip\n");
    if ((rc = flash_init(sfc, &chip))) {
        goto cleanup_soc;
    }

    logi("Write-protecting all chip-selects\n");
    if ((rc = sfc_write_protect_save(sfc, true, &wp)))
        goto cleanup_chip;

    if ((rc = sfc_get_flash(sfc, &flash)) < 0)
        goto cleanup_chip;

    logi("Exfiltrating BMC flash to stdout\n\n");
    rc = soc_siphon_out(soc, flash.start, chip->info.size, 1);
    if (rc) { errno = -rc; perror("soc_siphon_in"); }

    if ((cleanup = sfc_write_protect_restore(sfc, wp)) < 0) {
        errno = -rc;
        perror("sfc_write_protect_restore");
    }

cleanup_chip:
    flash_destroy(chip);

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    return rc;
}

static int cmd_read_ram(int argc, char *argv[])
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct soc_region dram, vram;
    unsigned long start, length;
    struct sdmc *sdmc;
    struct ahb *ahb;
    char *endp;
    int rc;

    /* FIXME: doesn't handle bridge argument parsing */
    if (argc >= 2) {
        errno = 0;
        start = strtoul(argv[0], &endp, 0);
        if (start == ULONG_MAX && errno) {
            loge("Failed to parse RAM region start address '%s': %s\n", argv[0], strerror(errno));
            return -errno;
#if ULONG_MAX > UINT32_MAX
        } else if (start > UINT32_MAX) {
            loge("RAM region start address '%s' exceeds address space\n", argv[0]);
            return -EINVAL;
#endif
        }
        argc--;
        argv++;

        errno = 0;
        length = strtoul(argv[0], &endp, 0);
        if (length == ULONG_MAX && errno) {
            loge("Failed to parse RAM region length '%s': %s\n", argv[0], strerror(errno));
            return -errno;
#if ULONG_MAX > UINT32_MAX
        } else if (length > UINT32_MAX) {
            loge("RAM region length '%s' exceeds address space\n", argv[0]);
            return -EINVAL;
#endif
        }
        argc--;
        argv++;
    } else {
        start = 0;
        length = 0;
    }

    if (UINT32_MAX - length < start) {
        return -EINVAL;
    }

    if ((rc = host_init(host, argc, argv)) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        return rc;
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = -ENODEV;
        goto cleanup_host;
    }

    if ((rc = soc_probe(soc, ahb)) < 0)
        goto cleanup_host;


    if (!(sdmc = sdmc_get(soc))) {
        rc = -ENODEV;
        goto cleanup_soc;
    }

    if ((rc = sdmc_get_dram(sdmc, &dram))) {
        goto cleanup_soc;
    }

    if (start && length) {
        if (start < dram.start || (start + length) > (dram.start + dram.length)) {
            rc = -EINVAL;
            goto cleanup_soc;
        }

        logi("Dumping %" PRId32 "MiB (%#.8" PRIx32 "-%#.8" PRIx32 ")",
             length >> 20, start, start + length - 1);
    } else {
        if ((rc = sdmc_get_vram(sdmc, &vram))) {
            goto cleanup_soc;
        }

        start = dram.start;
        length = dram.length - vram.length;

        logi("%dMiB DRAM with %dMiB VRAM; dumping %dMiB (0x%x-0x%08x)\n",
             dram.length >> 20, vram.length >> 20,
             (dram.length - vram.length) >> 20, dram.start, vram.start - 1);
    }

    rc = soc_siphon_out(soc, start, length, STDOUT_FILENO);
    if (rc) {
        errno = -rc;
        perror("soc_siphon_in");
    }

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    return rc;
}

static int do_read(const char *name __unused, int argc, char *argv[])
{
    int rc;

    if (argc < 1) {
        loge("Not enough arguments for read command\n");
        return EXIT_FAILURE;
    }

    if (!strcmp("firmware", argv[0])) {
        rc = cmd_read_firmware(argc - 1, argv + 1);
    } else if (!strcmp("ram", argv[0])) {
        rc = cmd_read_ram(argc - 1, argv + 1);
    } else {
        loge("Unsupported read type '%s'", argv[0]);
        rc = -EINVAL;
    }

    return rc < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

static const struct cmd read_cmd = {
    "read",
    "<firmware|ram ADDRESS LENGTH> [INTERFACE [IP PORT USERNAME PASSWORD]]",
    do_read,
};
REGISTER_CMD(read_cmd);
