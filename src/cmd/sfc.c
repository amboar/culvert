// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
#include "ahb.h"
#include "ast.h"
#include "compiler.h"
#include "flash.h"
#include "host.h"
#include "log.h"
#include "priv.h"
#include "soc/sfc.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SFC_FLASH_WIN (64 << 10)

enum flash_op { flash_op_read, flash_op_write, flash_op_erase };

int cmd_sfc(const char *name __unused, int argc, char *argv[])
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct flash_chip *chip;
    uint32_t offset, len;
    enum flash_op op;
    struct sfc *sfc;
    struct ahb *ahb;
    char *buf;
    int rc;

    if (argc < 4) {
        loge("Not enough arguments for sfc command\n");
        exit(EXIT_FAILURE);
    }

    if (strcmp("fmc", argv[0])) {
        loge("Unsupported sfc type: '%s'\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (!strcmp("read", argv[1])) {
        op = flash_op_read;
    } else if (!strcmp("write", argv[1])) {
        op = flash_op_write;
    } else if (!strcmp("erase", argv[1])) {
        op = flash_op_erase;
    } else {
        loge("Unsupported sfc operation: '%s'\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    offset = strtoul(argv[2], NULL, 0);
    len = strtoul(argv[3], NULL, 0);

    if ((rc = host_init(host, argc - 4, argv + 4)) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        exit(EXIT_FAILURE);
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = EXIT_FAILURE;
        goto cleanup_host;
    }

    rc = soc_probe(soc, ahb);
    if (rc < 0)
        goto cleanup_host;

    ;
    if (!(sfc = sfc_get_by_name(soc, "fmc"))) {
        loge("Failed to acquire SPI controller, exiting\n");
        goto cleanup_soc;
    }

    rc = flash_init(sfc, &chip);
    if (rc < 0)
        goto cleanup_soc;

    if (op == flash_op_read) {
        ssize_t egress;

        buf = malloc(len);
        if (!buf)
            goto cleanup_flash;

        rc = flash_read(chip, offset, buf, len);
        egress = write(1, buf, len);
        if (egress == -1) {
            rc = -errno;
            perror("write");
        }

        free(buf);
    } else if (op == flash_op_write) {
        ssize_t ingress;

        len = SFC_FLASH_WIN;
        buf = malloc(len);
        if (!buf)
            goto cleanup_flash;

        while ((ingress = read(0, buf, len))) {
            if (ingress < 0) {
                rc = -errno;
                break;
            }

            rc = flash_write(chip, offset, buf, ingress, true);
            if (rc < 0)
                break;

            offset += ingress;
        }

        free(buf);
    } else if (op == flash_op_erase) {
        rc = flash_erase(chip, offset, len);
    }

cleanup_flash:
    flash_destroy(chip);

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    return rc;
}
