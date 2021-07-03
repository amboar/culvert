// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ahb.h"
#include "ast.h"
#include "log.h"
#include "otp.h"
#include "priv.h"

int cmd_otp(const char *name, int argc, char *argv[])
{
    enum otp_region reg = otp_region_conf;
    struct ahb _ahb, *ahb = &_ahb;
    struct otp _otp, *otp = &_otp;
    bool rd = true;
    int argo = 2;
    int cleanup;
    int rc;

    if (argc < 2) {
        loge("Not enough arguments for otp command\n");
        exit(EXIT_FAILURE);
    }

    if (!strcmp("conf", argv[1]))
        reg = otp_region_conf;
    else if (!strcmp("strap", argv[1]))
        reg = otp_region_strap;
    else {
        loge("Unsupported otp region: %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    if (!strcmp("write", argv[0])) {
        rd = false;
        argo += 2;
    } else if (strcmp("read", argv[0])) {
        loge("Unsupported command: %s\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (argc < argo) {
        loge("Not enough arguments for otp command\n");
        exit(EXIT_FAILURE);
    }

    rc = ast_ahb_from_args(ahb, argc - argo, &argv[argo]);
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

    rc = otp_init(otp, ahb);
    if (rc < 0) {
        errno = -rc;
        perror("otp_init");
        cleanup = ahb_destroy(ahb);
        exit(EXIT_FAILURE);
    }

    if (rd)
        rc = otp_read(otp, reg);
    else {
        if (reg == otp_region_strap) {
            unsigned int bit;
            unsigned int val;

            bit = strtoul(argv[2], NULL, 0);
            val = strtoul(argv[3], NULL, 0);

            rc = otp_write_strap(otp, bit, val);
        } else {
            unsigned int word;
            unsigned int bit;

            word = strtoul(argv[2], NULL, 0);
            bit = strtoul(argv[3], NULL, 0);

            rc = otp_write_conf(otp, word, bit);
        }
    }

    cleanup = ahb_destroy(ahb);
    if (cleanup < 0) { errno = -cleanup; perror("ahb_destroy"); }

    return rc;
}

