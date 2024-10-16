// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.
// Copyright (C) 2021, Oracle and/or its affiliates.

#define _GNU_SOURCE
#include "ast.h"
#include "bridge/devmem.h"
#include "bridge/ilpc.h"
#include "bridge/p2a.h"
#include "log.h"
#include "mb.h"
#include "priv.h"
#include "rev.h"
#include "soc.h"
#include "soc/sdmc.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int ast_ahb_access(const char *name __unused, int argc, char *argv[],
                   struct ahb *ahb)
{
    uint32_t address, data;
    bool action_read;
    int rc;

    if (argc < 2) {
        loge("Not enough arguments for AHB access\n");
        exit(EXIT_FAILURE);
    }

    if (!strcmp("read", argv[0]))
        action_read = true;
    else if (!strcmp("write", argv[0]))
        action_read = false;
    else {
        loge("Unknown action: %s\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    address = strtoul(argv[1], NULL, 0);

    if (action_read) {
        unsigned long len = 4;

        if (argc >= 3) {
            len = strtoul(argv[2], NULL, 0);
        }

        if (len > 4) {
            if ((rc = ahb_siphon_out(ahb, address, len, STDOUT_FILENO))) {
                errno = -rc;
                perror("ahb_siphon_in");
                exit(EXIT_FAILURE);
            }
        } else {
            if ((rc = ahb_readl(ahb, address, &data))) {
                errno = -rc;
                perror("ahb_readl");
                exit(EXIT_FAILURE);
            }
            printf("0x%08x: 0x%08x\n", address, le32toh(data));
        }
    } else {
        unsigned long data;

        if (argc >= 3) {
            data = strtoul(argv[2], NULL, 0);
            if ((rc = ahb_writel(ahb, address, htole32(data)))) {
                errno = -rc;
                perror("ahb_writel");
                exit(EXIT_FAILURE);
            }
        } else {
            if ((rc = ahb_siphon_in(ahb, address, -1, STDIN_FILENO))) {
                errno = -rc;
                perror("ahb_writel");
                exit(EXIT_FAILURE);
            }
        }
    }

    return 0;
}
