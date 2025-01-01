// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
// Copyright (C) 2021, Oracle and/or its affiliates.

/* For program_invocation_short_name */
#define _GNU_SOURCE

#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd.h"
#include "config.h"
#include "compiler.h"
#include "log.h"
#include "version.h"
#include "ahb.h"
#include "host.h"

#include "ccan/autodata/autodata.h"

static void print_version(const char *name)
{
    printf("%s: " CULVERT_VERSION "\n", name);
}

static void print_help(const char *name, struct cmd **cmds, size_t n_cmds)
{
    print_version(name);
    printf("Usage:\n");
    printf("\n");

    for (size_t i = 0; i < n_cmds; i++) {
        printf("\t%s %s %s\n", name, cmds[i]->name, cmds[i]->help);
    }
}

int main(int argc, char *argv[])
{
    bool show_help = false;
    bool quiet = false;
    struct cmd **cmds;
    size_t n_cmds = 0;
    int verbose = 0;
    int rc;

    while (1) {
        static struct option long_options[] = {
            { "help", no_argument, NULL, 'h' },
            { "quiet", no_argument, NULL, 'q' },
            { "skip-bridge", required_argument, NULL, 's' },
            { "list-bridges", no_argument, NULL, 'l' },
            { "verbose", no_argument, NULL, 'v' },
            { "version", no_argument, NULL, 'V' },
            { },
        };
        int option_index = 0;
        int c;

        c = getopt_long(argc, argv, "+hlqs:vV", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'h':
                show_help = true;
                break;
            case 'l':
                print_bridge_drivers();
                exit(EXIT_SUCCESS);
                break;
            case 'v':
                verbose++;
                break;
            case 'V':
                print_version(program_invocation_short_name);
                exit(EXIT_SUCCESS);
            case 'q':
                quiet = true;
                break;
            case 's':
                if (disable_bridge_driver(optarg)) {
                    fprintf(stderr, "Error: '%s' not a recognized bridge name (use '-l' to list)\n", optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case '?':
                exit(EXIT_FAILURE);
            default:
                continue;
        }
    }

    cmds = autodata_get(cmds, &n_cmds);
    qsort(cmds, n_cmds, sizeof(void *), cmd_cmp);

    if (optind == argc) {
        if (show_help) {
            rc = 0;
        } else {
            fprintf(stderr, "Error: not enough arguments\n");
            rc = -EINVAL;
        }
        print_help(program_invocation_short_name, cmds, n_cmds);
        goto cleanup_cmds;
    }

    if (quiet) {
        log_set_level(level_none);
    } else if ((level_info + verbose) <= level_trace) {
        log_set_level(level_info + verbose);
    } else {
        log_set_level(level_trace);
    }

    rc = -EINVAL;
    for (size_t i = 0; i < n_cmds; i++) {
        struct cmd *cmd = cmds[i];
        int offset;

        if (strcmp(cmd->name, argv[optind])) {
            continue;
        }

        offset = optind;

        /* probe uses getopt, but for subcommands not using getopt */
        if (!(!strcmp("probe", argv[optind]) || !strcmp("write", argv[optind]))) {
            offset += 1;
        }
        optind = 1;

        rc = cmd->fn(program_invocation_short_name, argc - offset, argv + offset);
        break;
    }

    if (rc == -EINVAL) {
        fprintf(stderr, "Unrecognised command\n\n");
        print_help(program_invocation_short_name, cmds, n_cmds);
    }

cleanup_cmds:
    autodata_free(cmds);
    exit(rc ? EXIT_FAILURE : EXIT_SUCCESS);
}
