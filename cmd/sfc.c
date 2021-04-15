// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ahb.h"
#include "ast.h"
#include "flash.h"
#include "log.h"
#include "priv.h"
#include "sfc.h"

#define SFC_FLASH_WIN (64 << 10)

enum flash_op { flash_op_read, flash_op_write, flash_op_erase };

int cmd_sfc(const char *name, int argc, char *argv[])
{
    struct ahb _ahb, *ahb = &_ahb;
    struct flash_chip *chip;
    uint32_t offset, len;
    enum flash_op op;
    struct sfc *sfc;
    int rc, cleanup;
    char *buf;

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

    rc = ast_ahb_from_args(ahb, argc - 4, argv + 4);
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

    rc = sfc_init(&sfc, ahb, SFC_TYPE_FMC);
    if (rc < 0)
        goto cleanup_ahb;

    rc = flash_init(sfc, &chip);
    if (rc < 0)
        goto cleanup_sfc;

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

cleanup_sfc:
    sfc_destroy(sfc);

cleanup_ahb:
    cleanup = ahb_destroy(ahb);
    if (cleanup < 0) { errno = -cleanup; perror("ahb_destroy"); }

    return rc;
}

