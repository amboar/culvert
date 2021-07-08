// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ahb.h"
#include "ast.h"
#include "clk.h"
#include "flash.h"
#include "log.h"
#include "priv.h"
#include "sfc.h"
#include "uart/vuart.h"
#include "wdt.h"

#define SFC_FLASH_WIN (64 << 10)

int cmd_write(const char *name, int argc, char *argv[])
{
    struct vuart _vuart, *vuart = &_vuart;
    struct ahb _ahb, *ahb = &_ahb;
    struct soc _soc, *soc = &_soc;
    struct flash_chip *chip;
    bool live = false;
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
        loge("Unsupported write type '%s'", argv[0]);
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
                logi("BMC is live, will take actions to halt its execution\n");
                live = true;
                break;
        }
    }

    rc = ast_ahb_from_args(ahb, argc - optind, &argv[optind]);
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
    if (rc < 0) {
        errno = -rc;
        perror("soc_probe");
        exit(EXIT_FAILURE);
    }

    if (ahb->bridge == ahb_devmem)
        loge("I hope you know what you are doing\n");
    else if (live) {
        logi("Preventing system reset\n");
        rc = wdt_prevent_reset(soc);
        if (rc < 0)
            goto cleanup_ahb;

        logi("Gating ARM clock\n");
        rc = clk_disable(ahb, clk_arm);
        if (rc < 0)
            goto cleanup_soc;

        rc = vuart_init(vuart, soc, "vuart");
        if (rc < 0)
            goto cleanup_clk;

        logi("Configuring VUART for host Tx discard\n");
        rc = vuart_set_host_tx_discard(vuart, discard_enable);
        if (rc < 0)
            goto cleanup_clk;
    }

    logi("Initialising flash subsystem\n");
    rc = sfc_init(&sfc, ahb, SFC_TYPE_FMC);
    if (rc < 0)
        goto cleanup_vuart;

    rc = flash_init(sfc, &chip);
    if (rc < 0)
        goto cleanup_sfc;

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
                goto cleanup_flash;
            }

            rc = flash_erase(chip, phys, ingress);
            if (rc < 0)
                goto cleanup_flash;

            rc = flash_write(chip, phys, buf, ingress, true);
            if (rc < 0)
                break;
        } while (rc == -EREMOTEIO); /* Miscompare */

        phys += ingress;
    }

    free(buf);

cleanup_flash:
    flash_destroy(chip);

cleanup_sfc:
    sfc_destroy(sfc);

cleanup_soc:
    if (live && !rc && ahb->bridge != ahb_devmem) {
        struct wdt _wdt, *wdt = &_wdt;
        int64_t wait;

        logi("Performing SoC reset\n");
        rc = wdt_init(wdt, soc, "wdt2");
        if (rc < 0) {
            goto cleanup_clk;
        }

        wait = wdt_perform_reset(wdt);

        wdt_destroy(wdt);

        if (wait < 0) {
            rc = wait;
            goto cleanup_clk;
        }

        usleep(wait);
    }

cleanup_vuart:
    if (live) {
        logi("Deconfiguring VUART host Tx discard\n");
        cleanup = vuart_set_host_tx_discard(vuart, discard_disable);
        if (cleanup) { errno = -cleanup; perror("vuart_set_host_tx_discard"); }

        vuart_destroy(vuart);
    }

cleanup_clk:
    if (live && rc < 0) {
        logi("Ungating ARM clock\n");
        cleanup = clk_enable(ahb, clk_arm);
        if (cleanup) { errno = -cleanup; perror("clk_enable"); }
    }

cleanup_ahb:
    soc_destroy(soc);

    cleanup = ahb_cleanup(ahb);
    if (cleanup) { errno = -cleanup; perror("ahb_cleanup"); }

    if (rc < 0)
        exit(EXIT_FAILURE);

    return 0;
}

