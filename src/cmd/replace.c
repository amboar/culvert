// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
#define _GNU_SOURCE

#include "arg_helper.h"
#include "ahb.h"
#include "ast.h"
#include "compiler.h"
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

static char doc[] =
    "\n"
    "Replace command\n"
    "\v"
    "Replace something in the BMC RAM\n"
    "\n"
    "Supported space types:\n"
    "  ram     BMC RAM\n"
    "\n"
    "Example:\n"
    "\n"
    "  culvert replace -t ram -m 'old string' -r 'new string'\n";

struct cmd_replace_args {
    char *type;
    char *match;
    char *replace;
    size_t replace_len;
    size_t match_len;
    int key_arg_count;
};

static struct argp_option options[] = {
    {"type", 't', "TYPE", 0, "Space to replace (required)", 0},
    {"match", 'm', "MATCH", 0, "String to match (required)", 0},
    {"replace", 'r', "REPLACE", 0, "String to replace (required)", 0},
    {0},
};

static error_t
parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_replace_args *arguments = state->input;

    if (key == ARGP_KEY_ARG)
        arguments->key_arg_count++;

    switch (key) {
    case 't':
        if (strcmp("ram", arg))
            argp_error(state, "Invalid space '%s'", arg);
        arguments->type = arg;
        break;
    case 'm':
        arguments->match_len = strlen(arg);
        arguments->match = arg;
        break;
    case 'r':
        arguments->replace_len = strlen(arg);
        arguments->replace = arg;
        break;
    case ARGP_KEY_ARG:
    case ARGP_KEY_END:
        if (!arguments->type)
            argp_error(state, "Missing space type");
        if (!arguments->match_len)
            argp_error(state, "Missing match");
        if (!arguments->replace_len)
            argp_error(state, "Missing replace");

        if (arguments->replace_len > arguments->match_len)
            argp_error(state, "REPLACE length %zd overruns MATCH length %zd",
                       arguments->replace_len, arguments->match_len);
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp argp = {
    options,
    parse_opt,
    "[INTERFACE [IP PORT USERNAME PASSWORD]]",
    doc,
    NULL,
    NULL,
    NULL,
};

int cmd_replace(struct argp_state *state)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct soc_region dram, vram;
    struct sdmc *sdmc;
    size_t ram_cursor;
    struct ahb *ahb;
    void *win_chunk;
    void *needle;
    int rc;

    struct subcommand replace_cmd;
    struct cmd_replace_args arguments = {0};
    parse_subcommand(&argp, "replace", &arguments, state, &replace_cmd);

    char **host_argv = replace_cmd.argv + 1 + (replace_cmd.argc - 1 - arguments.key_arg_count);

    win_chunk = malloc(DUMP_RAM_WIN);
    if (!win_chunk) { perror("malloc"); exit(EXIT_FAILURE); }

    if ((rc = host_init(host, arguments.key_arg_count, host_argv)) < 0) {
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
                                arguments.match, arguments.match_len))) {
            logi("0x%08zx: Replacing '%s' with '%s'\n",
                 ram_cursor + (needle - win_chunk),
                               arguments.match, arguments.replace);
            rc = ahb_write(ahb, ram_cursor + (needle - win_chunk),
                           arguments.replace, arguments.replace_len);
            if (rc < 0) {
                errno = -rc;
                perror("l2ab_write");
                break;
            } else if ((size_t)rc != arguments.replace_len) {
                loge("Short write: %d\n", rc);
                break;
            }

            if ((needle + arguments.replace_len) > win_chunk + DUMP_RAM_WIN)
                break;

            needle += arguments.replace_len;
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
