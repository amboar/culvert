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

int cmd_write(const char *name __unused, int argc, char *argv[])
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct flash_chip *chip;
    struct vuart *vuart;
    struct ahb *ahb;
    struct clk *clk;
    ssize_t ingress;
    struct sfc *sfc;
    int rc, cleanup;
    uint32_t phys;
    char *buf;

    if (argc < 1) {
        loge("Not enough arguments for write command\n");
        exit(EXIT_FAILURE);
    }

    if (strcmp("firmware", argv[0])) {
        loge("Unsupported write type '%s'\n", argv[0]);
        return -EINVAL;
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
                exit(EXIT_FAILURE);
        }
    }

    if ((rc = host_init(host, argc - optind, argv + optind)) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        exit(EXIT_FAILURE);
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = EXIT_FAILURE;
        goto cleanup_host;
    }

    if ((rc = soc_probe(soc, ahb)) < 0) {
        errno = -rc;
        perror("soc_probe");
        goto cleanup_host;
    }

    if (!(clk = clk_get(soc))) {
        loge("Failed to acquire clock controller, exiting\n");
        goto cleanup_soc;
    }

    if (!(vuart = vuart_get_by_name(soc, "vuart"))) {
        loge("Failed to acquire VUART controller, exiting\n");
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
        goto cleanup_state;
    }

    rc = flash_init(sfc, &chip);
    if (rc < 0)
        goto cleanup_state;

    /* FIXME: Make this common with the sfc write implementation */
    buf = malloc(SFC_FLASH_WIN);
    if (!buf)
        goto cleanup_flash;

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
                goto cleanup_soc;
            }

            rc = wdt_perform_reset(wdt);
            if (rc < 0)
                goto cleanup_soc;
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

