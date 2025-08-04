// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
#define _GNU_SOURCE

#include "ahb.h"
#include "ast.h"
#include "cmd.h"
#include "compiler.h"
#include "connection.h"
#include "host.h"
#include "log.h"
#include "priv.h"
#include "soc.h"
#include "soc/sdmc.h"

#include <argp.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DUMP_RAM_WIN  (8 << 20)

static char cmd_replace_args_doc[] =
    "ram <MATCH> <REPLACE> "
    "[via DRIVER [INTERFACE [IP PORT USERNAME PASSWORD]]]";

static char cmd_replace_doc[] =
    "\n"
    "Replace commnd"
    "\v"
    "Replace a portion in the memory.\n";

static struct argp_option cmd_replace_options[] = {
    {0},
};

struct cmd_replace_args {
    const char *mem_match;
    const char *mem_replace;
    struct connection_args connection;
};

static error_t cmd_replace_parse_opt(int key, char *arg,
                                     struct argp_state *state)
{
    struct cmd_replace_args *arguments = state->input;
    int rc;

    switch (key) {
    case ARGP_KEY_ARG:
        if (!strcmp(arg, "via")) {
            rc = cmd_parse_via(state->next - 1, state, &arguments->connection);
            if (rc != 0)
                argp_error(state, "Failed to parse connection arguments. Returned code %d", rc);
            break;
        }

        switch (state->arg_num) {
        case 0:
            if (strcmp(arg, "ram"))
                argp_error(state, "Invalid type '%s' found. Only 'ram' is allowed", arg);
            break;
        case 1:
            arguments->mem_match = arg;
            break;
        case 2:
            arguments->mem_replace = arg;
            break;
        }
        break;
    case ARGP_KEY_END:
        if (state->arg_num < 3)
            argp_error(state, "Not enough arguments...");

        if (strlen(arguments->mem_match) < strlen(arguments->mem_replace))
            argp_error(state, "REPLACE length %zd overruns MATCH length %zd, bailing",
                 strlen(arguments->mem_match), strlen(arguments->mem_replace));
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp cmd_replace_argp = {
    .options = cmd_replace_options,
    .parser = cmd_replace_parse_opt,
    .args_doc = cmd_replace_args_doc,
    .doc = cmd_replace_doc,
};

static int do_replace(int argc, char **argv)
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

    struct cmd_replace_args arguments = {0};
    rc = argp_parse(&cmd_replace_argp, argc, argv, ARGP_IN_ORDER, 0, &arguments);
    if (rc != 0)
        return rc;

    win_chunk = malloc(DUMP_RAM_WIN);
    if (!win_chunk) { perror("malloc"); exit(EXIT_FAILURE); }

    if ((rc = host_init(host, &arguments.connection)) < 0) {
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

    replace_len = strlen(arguments.mem_match);
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
                                arguments.mem_match, replace_len))) {
            logi("0x%08zx: Replacing '%s' with '%s'\n",
                 ram_cursor + (needle - win_chunk), arguments.mem_match, arguments.mem_replace);
            rc = ahb_write(ahb, ram_cursor + (needle - win_chunk), arguments.mem_replace,
                           strlen(arguments.mem_replace));
            if (rc < 0) {
                errno = -rc;
                perror("l2ab_write");
                break;
            } else if ((size_t)rc != strlen(arguments.mem_replace)) {
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

static const struct cmd replace_cmd = {
    .name = "replace",
    .description = "Replace a portion in the memory",
    .fn = do_replace,
};
REGISTER_CMD(replace_cmd);
