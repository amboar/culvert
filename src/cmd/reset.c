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
    struct wdt _wdt, *wdt = &_wdt;
    int64_t wait = 0;
    int cleanup;
    int rc;

    if (argc < 2) {
        loge("Not enough arguments for reset command\n");
        exit(EXIT_FAILURE);
    }

    if (strcmp("soc", argv[0])) {
        loge("Unsupported reset type: '%s'\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    rc = ast_ahb_from_args(ahb, argc - 2, argv + 2);
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

    if (ahb->bridge != ahb_devmem) {
        logi("Gating ARM clock\n");
        rc = clk_disable(ahb, clk_arm);
        if (rc < 0)
            goto cleanup_ahb;
    }

    logi("Preventing system reset\n");
    rc = wdt_prevent_reset(soc);
    if (rc < 0)
        goto cleanup_clk;

    logi("Performing SoC reset\n");
    rc = wdt_init(wdt, soc, argv[1]);
    if (rc < 0)
        goto cleanup_clk;

    wait = wdt_perform_reset(wdt);

    wdt_destroy(wdt);

    if (wait < 0) {
cleanup_clk:
        logi("Ungating ARM clock\n");
        rc = clk_enable(ahb, clk_arm);
    }

cleanup_ahb:
    cleanup = ahb_cleanup(ahb);
    if (cleanup < 0) { errno = -cleanup; perror("ahb_destroy"); }

    if (wait)
        usleep(wait);

    return rc;
}
