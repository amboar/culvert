// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
#include "ahb.h"
#include "ast.h"
#include "compiler.h"
#include "host.h"
#include "log.h"
#include "otp.h"
#include "priv.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_otp(const char *name __unused, int argc, char *argv[])
{
    enum otp_region reg = otp_region_conf;
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct otp *otp;
    struct ahb *ahb;
    bool rd = true;
    int argo = 2;
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

    if ((rc = host_init(host, argc - argo, argv + argo)) < 0) {
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

    if (!(otp = otp_get(soc))) {
        loge("Failed to acquire OTP controller, exiting\n");
        rc = EXIT_FAILURE;
        goto cleanup_soc;
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

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    return rc;
}
