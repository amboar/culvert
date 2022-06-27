// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
#define _GNU_SOURCE

#include "ahb.h"
#include "ast.h"
#include "compiler.h"
#include "host.h"
#include "log.h"
#include "priv.h"
#include "sdmc.h"
#include "soc.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DUMP_RAM_WIN  (8 << 20)

int cmd_replace(const char *name __unused, int argc, char *argv[])
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct soc_region dram, vram;
    size_t replace_len;
    struct sdmc *sdmc;
    size_t ram_cursor;
    struct ahb *ahb;
    void *win_chunk;
    void *needle;
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

    if ((rc = host_init(host, argc - 3, argv + 3)) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        rc = EXIT_FAILURE;
        goto win_chunk_cleanup;
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = EXIT_FAILURE;
        goto host_cleanup;
    }

    if ((rc = soc_probe(soc, ahb)))
        goto host_cleanup;

    if (!(sdmc = sdmc_get(soc))) {
        loge("Failed to acquire memory controller, exiting\n");
        rc = EXIT_FAILURE;
        goto soc_cleanup;
    }

    if ((rc = sdmc_get_dram(sdmc, &dram)))
        goto soc_cleanup;

    if ((rc = sdmc_get_vram(sdmc, &vram)))
        goto soc_cleanup;

    replace_len = strlen(argv[1]);
    for (ram_cursor = dram.start;
         ram_cursor < vram.start;
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
            } else if ((size_t)rc != strlen(argv[2])) {
                loge("Short write: %d\n", rc);
                break;
            }

            if ((needle + replace_len) > win_chunk + DUMP_RAM_WIN)
                break;

            needle += replace_len;
        }
    }

soc_cleanup:
    soc_destroy(soc);

host_cleanup:
    host_destroy(host);

win_chunk_cleanup:
    free(win_chunk);

    return rc;
}
