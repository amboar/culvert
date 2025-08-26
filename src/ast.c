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

/**
 * Access raw data via the AHB bridge in the memory-mapped regions.
 *
 * Read behaviour: If the default read width of 4 is being overwritten,
 * then the data will be written to stdout without any modification.
 * This may be useful for reading FMC regions.
 *
 * Write behaviour: If the pointer of the write value is NULL, then it
 * will need data to be written to stdin.
 */
int ast_ahb_access(struct ast_ahb_args *args, struct ahb *ahb)
{
    uint32_t data;
    int rc;

    if (args->read) {
        unsigned long len = 4;

        if (args->read_length != NULL)
            len = *args->read_length;

        if (len > 4) {
            if ((rc = ahb_siphon_out(ahb, args->address, len, STDOUT_FILENO))) {
                errno = -rc;
                perror("ahb_siphon_in");
                exit(EXIT_FAILURE);
            }
        } else {
            if ((rc = ahb_readl(ahb, args->address, &data))) {
                errno = -rc;
                perror("ahb_readl");
                exit(EXIT_FAILURE);
            }
            printf("0x%08x: 0x%08x\n", args->address, le32toh(data));
        }
    } else {
        if (args->write_value != NULL) {
            if ((rc = ahb_writel(ahb, args->address, htole32(*args->write_value)))) {
                errno = -rc;
                perror("ahb_writel");
                exit(EXIT_FAILURE);
            }
        } else {
            if ((rc = ahb_siphon_in(ahb, args->address, -1, STDIN_FILENO))) {
                errno = -rc;
                perror("ahb_writel");
                exit(EXIT_FAILURE);
            }
        }
    }

    return 0;
}
