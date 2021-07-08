// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ahb.h"
#include "ast.h"
#include "clk.h"
#include "log.h"
#include "priv.h"
#include "soc.h"
#include "wdt.h"

int cmd_reset(const char *name, int argc, char *argv[])
{
    struct ahb _ahb, *ahb = &_ahb;
    struct soc _soc, *soc = &_soc;
    struct clk _clk, *clk = &_clk;
    struct wdt _wdt, *wdt = &_wdt;
    int64_t wait = 0;
    int cleanup;
    int rc;

    /* reset subcommand argument validation and parsing */
    if (argc < 2) {
        loge("Not enough arguments for reset command\n");
        exit(EXIT_FAILURE);
    }

    if (strcmp("soc", argv[0])) {
        loge("Unsupported reset type: '%s'\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /* Initialise the AHB bridge */
    if ((rc = ast_ahb_from_args(ahb, argc - 2, argv + 2)) < 0) {
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

    /* Probe the SoC */
    if ((rc = soc_probe(soc, ahb)) < 0) {
        errno = -rc;
        perror("soc_probe");
        goto cleanup_ahb;
    }

    /* Initialise the required SoC drivers */
    if ((rc = clk_init(clk, soc))) {
        errno = -rc;
        perror("clk_init");
        goto cleanup_soc;
    }

    if ((rc = wdt_init(wdt, soc, argv[1])) < 0) {
        errno = -rc;
        perror("wdt_init");
        goto cleanup_clk;
    }

    /* Do the reset */
    if (ahb->bridge != ahb_devmem) {
        logi("Gating ARM clock\n");
        rc = clk_disable(clk, clk_arm);
        if (rc < 0)
            goto cleanup_wdt;
    }

    logi("Preventing system reset\n");
    if ((rc = wdt_prevent_reset(soc)) < 0) {
        errno = -rc;
        perror("wdt_prevent_reset");
        goto clk_enable_arm;
    }

    /* wdt_perform_reset ungates the ARM if required */
    logi("Performing SoC reset\n");
    if ((wait = wdt_perform_reset(wdt)) > 0)
        usleep(wait);

    /* Cleanup */
    if (wait < 0) {
clk_enable_arm:
        if ((cleanup = clk_enable(clk, clk_arm)) < 0) {
            errno = -cleanup;
            perror("clk_enable");
        }
    }

cleanup_wdt:
    wdt_destroy(wdt);

cleanup_clk:
    clk_destroy(clk);

cleanup_soc:
    soc_destroy(soc);

cleanup_ahb:
    if ((cleanup = ahb_cleanup(ahb)) < 0) {
        errno = -cleanup;
        perror("ahb_destroy");
    }

    return rc;
}
