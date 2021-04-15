// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ahb.h"
#include "ast.h"
#include "log.h"
#include "priv.h"

#define DUMP_RAM_WIN  (8 << 20)

int cmd_replace(const char *name, int argc, char *argv[])
{
    struct ahb _ahb, *ahb = &_ahb;
    uint32_t scu_rev, sdmc_conf;
    uint32_t dram, vram, aram;
    size_t replace_len;
    size_t ram_cursor;
    void *win_chunk;
    void *needle;
    int cleanup;
    int rc;

    if (argc < 3) {
        loge("Not enough arguments for replace command\n");
        exit(EXIT_FAILURE);
    }

    if (strcmp("ram", argv[0])) {
        loge("Unsupported replace space: '%s'\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (strlen(argv[1]) < strlen(argv[2])) {
        loge("REPLACE length %zd overruns MATCH length %zd, bailing\n",
             strlen(argv[1]), strlen(argv[2]));
        exit(EXIT_FAILURE);
    }

    win_chunk = malloc(DUMP_RAM_WIN);
    if (!win_chunk) { perror("malloc"); exit(EXIT_FAILURE); }

    rc = ast_ahb_init(ahb, true);
    if (rc < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(name);
        } else if (rc == -ENOTSUP) {
            loge("Probes failed, cannot access BMC AHB\n");
        } else {
            errno = -rc;
            perror("ast_ahb_init");
        }
        exit(EXIT_FAILURE);
    }

    /* Test BMC silicon revision to make sure we use the right memory map */
    logi("Checking ASPEED BMC silicon revision\n");
    rc = ahb_readl(ahb, 0x1e6e207c, &scu_rev);
    if (rc) { errno = -rc; perror("ahb_readl"); goto ahb_cleanup; }

    if ((scu_rev >> 24) != 0x04) {
        loge("Unsupported BMC revision: 0x%08x\n", scu_rev);
        goto ahb_cleanup;
    }

    logi("Found AST2500-family BMC\n");

    rc = ahb_readl(ahb, 0x1e6e0004, &sdmc_conf);
    if (rc) { errno = -rc; perror("ahb_readl"); goto ahb_cleanup; }

    dram = bmc_dram_sizes[sdmc_conf & 0x03];
    vram = bmc_vram_sizes[(sdmc_conf >> 2) & 0x03];
    aram = dram - vram; /* Accessible DRAM */

    replace_len = strlen(argv[1]);
    for (ram_cursor = AST_G5_DRAM;
         ram_cursor < AST_G5_DRAM + aram;
         ram_cursor += DUMP_RAM_WIN) {
        logi("Scanning BMC RAM in range 0x%08zx-0x%08zx\n",
             ram_cursor, ram_cursor + DUMP_RAM_WIN - 1);
        rc = ahb_read(ahb, ram_cursor, win_chunk, DUMP_RAM_WIN);
        if (rc < 0) {
            errno = -rc;
            perror("l2ab_read");
            break;
        } else if (rc != DUMP_RAM_WIN) {
            loge("Short read: %d\n", rc);
            break;
        }

        /* FIXME: Handle sub-strings at the right hand edge */
        needle = win_chunk;
        while ((needle = memmem(needle, win_chunk + DUMP_RAM_WIN - needle,
                                argv[1], strlen(argv[1])))) {
            logi("0x%08zx: Replacing '%s' with '%s'\n",
                 ram_cursor + (needle - win_chunk), argv[1], argv[2]);
            rc = ahb_write(ahb, ram_cursor + (needle - win_chunk), argv[2],
                           strlen(argv[2]));
            if (rc < 0) {
                errno = -rc;
                perror("l2ab_write");
                break;
            } else if (rc != strlen(argv[2])) {
                loge("Short write: %d\n", rc);
                break;
            }

            if ((needle + replace_len) > win_chunk + DUMP_RAM_WIN)
                break;

            needle += replace_len;
        }
    }

ahb_cleanup:
    cleanup = ahb_destroy(ahb);
    if (cleanup) { errno = -cleanup; perror("ahb_destroy"); }

    free(win_chunk);

    return rc;
}
