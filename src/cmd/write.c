// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
#include "ahb.h"
#include "ast.h"
#include "compiler.h"
#include "flash.h"
#include "host.h"
#include "log.h"
#include "priv.h"
#include "soc/clk.h"
#include "soc/sdmc.h"
#include "soc/sfc.h"
#include "soc/uart/vuart.h"
#include "soc/wdt.h"

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SFC_FLASH_WIN (64 << 10)

static int cmd_write_firmware(int argc, char *argv[])
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct ahb *ahb;
    struct flash_chip *chip;
    struct vuart *vuart;
    struct clk *clk;
    ssize_t ingress;
    struct sfc *sfc;
    int rc, cleanup;
    uint32_t phys;
    char *buf;

    if (strcmp("firmware", argv[0])) {
        loge("Expected 'firmware' command, found '%s'\n", argv[0]);
        return -EINVAL;
    }

    argc--;
    argv++;

    if ((rc = host_init(host, argc, argv)) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        return rc;
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = -ENODEV;
        goto cleanup_host;
    }

    if ((rc = soc_probe(soc, ahb)) < 0) {
        loge("Failed to probe SoC: %d\n", rc);
        goto cleanup_host;
    }

    if (!(clk = clk_get(soc))) {
        loge("Failed to acquire clock controller, exiting\n");
        rc = -ENODEV;
        goto cleanup_soc;
    }

    if (!(vuart = vuart_get_by_name(soc, "vuart"))) {
        loge("Failed to acquire VUART controller, exiting\n");
        rc = -ENODEV;
        goto cleanup_soc;
    }

    if (ahb->drv->local)
        loge("I hope you know what you are doing\n");
    else {
        logi("Preventing system reset\n");
        if ((rc = wdt_prevent_reset(soc)) < 0)
            goto cleanup_state;

        logi("Gating ARM clock\n");
        if ((rc = clk_disable(clk, clk_arm)) < 0)
            goto cleanup_state;

        logi("Configuring VUART for host Tx discard\n");
        if ((rc = vuart_set_host_tx_discard(vuart, discard_enable)) < 0)
            goto cleanup_state;
    }

    logi("Initialising flash subsystem\n");
    if (!(sfc = sfc_get_by_name(soc, "fmc"))) {
        loge("Failed to acquire SPI flash controller, exiting\n");
        rc = -ENODEV;
        goto cleanup_state;
    }

    rc = flash_init(sfc, &chip);
    if (rc < 0)
        goto cleanup_state;

    /* FIXME: Make this common with the sfc write implementation */
    buf = malloc(SFC_FLASH_WIN);
    if (!buf) {
        rc = -ENOMEM;
        goto cleanup_flash;
    }

    logi("Writing firmware image\n");
    phys = 0;
    while ((ingress = read(0, buf, SFC_FLASH_WIN))) {
        if (ingress < 0) {
            rc = -errno;
            break;
        }

        do {
            if (ingress < SFC_FLASH_WIN) {
                loge("Unexpected ingress value: 0x%zx\n", ingress);
                goto cleanup_buf;
            }

            rc = flash_erase(chip, phys, ingress);
            if (rc < 0)
                goto cleanup_buf;

            rc = flash_write(chip, phys, buf, ingress, true);
        } while (rc == -EREMOTEIO); /* Miscompare */

        if (rc)
            break;

        phys += ingress;
    }

cleanup_buf:
    free(buf);

cleanup_flash:
    flash_destroy(chip);

cleanup_state:
    if (rc == 0) {
        if (!ahb->drv->local) {
            struct wdt *wdt;

            logi("Performing SoC reset\n");
            if (!(wdt = wdt_get_by_name(soc, "wdt2"))) {
                loge("Failed to acquire wdt2 controller, exiting\n");
                return -ENODEV;
            }

            rc = wdt_perform_reset(wdt);
            if (rc < 0) {
                return rc;
            }
        }
    } else {
        logi("Deconfiguring VUART host Tx discard\n");
        if ((cleanup = vuart_set_host_tx_discard(vuart, discard_disable))) {
            errno = -cleanup;
            perror("vuart_set_host_tx_discard");
        }

        logi("Ungating ARM clock\n");
        if ((cleanup = clk_enable(clk, clk_arm))) {
            errno = -cleanup;
            perror("clk_enable");
        }
    }

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    return rc;
}

static int cmd_write_ram(int argc, char *argv[])
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    unsigned long start, length;
    struct soc_region dram;
    struct sdmc *sdmc;
    struct ahb *ahb;
    int rc;
    char *endp;

    /* FIXME: doesn't handle bridge argument parsing */
    if (argc < 3) {
        loge("Not enough arguments for `write ram` command\n");
        return -EINVAL;
    }

    if (strcmp("ram", argv[0])) {
        loge("Expected 'ram' command, found '%s'\n", argv[0]);
        return -EINVAL;
    }

    argc--;
    argv++;

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

    if ((rc = host_init(host, argc, argv)) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        return rc;
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = EXIT_FAILURE;
        goto cleanup_host;
    }

    if ((rc = soc_probe(soc, ahb)) < 0) {
        loge("Failed to probe SoC: %d\n", rc);
        goto cleanup_host;
    }

    if (!(sdmc = sdmc_get(soc))) {
        loge("Failed to acquire SDRAM memory controller\n");
        rc = -ENODEV;
        goto cleanup_soc;
    }

    if ((rc = sdmc_get_dram(sdmc, &dram))) {
        loge("Failed to locate DRAM: %d\n", rc);
        goto cleanup_soc;
    }

    if (((start + length) & UINT32_MAX) < start) {
        loge("Invalid RAM region provided for write\n");
        rc = -EINVAL;
        goto cleanup_soc;
    }

    if (start < dram.start || (start + length) > (dram.start + dram.length)) {
        loge("Ill-formed RAM region provided for write\n");
        rc = -EINVAL;
        goto cleanup_soc;
    }

#if UINT32_MAX > SSIZE_MAX
    if (SSIZE_MAX < length) {
        return -EINVAL;
    }
#endif

    if ((rc = soc_siphon_in(soc, start, length, STDIN_FILENO))) {
        loge("Failed to write to provided memory region: %d\n", rc);
    }

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    return rc;
}

int cmd_write(const char *name __unused, int argc, char *argv[])
{
    int rc;

    if (argc < 1) {
        loge("Not enough arguments for write command\n");
        exit(EXIT_FAILURE);
    }

    /* Do option parsing for the "firmware" write type */
    while (1) {
        int option_index = 0;
        int c;

        static struct option long_options[] = {
            { "live", no_argument, NULL, 'l' },
            { },
        };

        c = getopt_long(argc, argv, "l", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'l':
                /* no-op flag retained for backwards compatibility */
                break;
            case '?':
                return -EINVAL;
        }
    }

    if (!strcmp("firmware", argv[optind])) {
        rc = cmd_write_firmware(argc - optind, &argv[optind]);
    } else if (!strcmp("ram", argv[optind])) {
        rc = cmd_write_ram(argc - optind, &argv[optind]);
    } else {
        loge("Unsupported write type '%s'\n", argv[optind]);
        rc = -EINVAL;
    }

    return rc;
}

