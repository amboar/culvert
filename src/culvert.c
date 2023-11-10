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

#include "config.h"
#include "log.h"

int cmd_ilpc(const char *name, int argc, char *argv[]);
int cmd_p2a(const char *name, int argc, char *argv[]);
int cmd_debug(const char *name, int argc, char *argv[]);
int cmd_devmem(const char *name, int argc, char *argv[]);
int cmd_console(const char *name, int argc, char *argv[]);
int cmd_read(const char *name, int argc, char *argv[]);
int cmd_write(const char *name, int argc, char *argv[]);
int cmd_replace(const char *name, int argc, char *argv[]);
int cmd_probe(const char *name, int argc, char *argv[]);
int cmd_reset(const char *name, int argc, char *argv[]);
int cmd_sfc(const char *name, int argc, char *argv[]);
int cmd_otp(const char *name, int argc, char *argv[]);
int cmd_trace(const char *name, int argc, char *argv[]);

static void help(const char *name)
{
    printf("%s: " VERSION "\n", name);
    printf("Usage:\n");
    printf("\n");
    printf("%s probe [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s ilpc read ADDRESS\n", name);
    printf("%s ilpc write ADDRESS VALUE\n", name);
    printf("%s p2a vga read ADDRESS\n", name);
    printf("%s p2a vga write ADDRESS VALUE\n", name);
    printf("%s debug read ADDRESS INTERFACE [IP PORT USERNAME PASSWORD]\n", name);
    printf("%s debug write ADDRESS VALUE INTERFACE [IP PORT USERNAME PASSWORD]\n", name);
    printf("%s devmem read ADDRESS\n", name);
    printf("%s devmem write ADDRESS VALUE\n", name);
    printf("%s console HOST_UART BMC_UART BAUD USER PASSWORD\n", name);
    printf("%s read firmware [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s read ram [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s write firmware [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s replace ram MATCH REPLACE\n", name);
    printf("%s reset TYPE WDT [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s sfc fmc read ADDRESS LENGTH [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s sfc fmc erase ADDRESS LENGTH [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s sfc fmc write ADDRESS LENGTH [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s otp read conf [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s otp read strap [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s otp write strap BIT VALUE [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s otp write conf WORD BIT [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s trace ADDRESS WIDTH MODE [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
}

struct command {
    const char *name;
    int (*fn)(const char *, int, char *[]);
};

static const struct command cmds[] = {
    { "ilpc", cmd_ilpc },
    { "p2a", cmd_p2a },
    { "console", cmd_console },
    { "read", cmd_read },
    { "write", cmd_write },
    { "replace", cmd_replace },
    { "probe", cmd_probe },
    { "debug", cmd_debug },
    { "reset", cmd_reset },
    { "devmem", cmd_devmem },
    { "sfc", cmd_sfc },
    { "otp", cmd_otp },
    { "trace", cmd_trace },
    { },
};

int main(int argc, char *argv[])
{
    const struct command *cmd = &cmds[0];
    bool show_help = false;
    bool quiet = false;
    int verbose = 0;

    while (1) {
        static struct option long_options[] = {
            { "help", no_argument, NULL, 'h' },
            { "quiet", no_argument, NULL, 'q' },
            { "verbose", no_argument, NULL, 'v' },
            { },
        };
        int option_index = 0;
        int c;

        c = getopt_long(argc, argv, "+hqv", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'h':
                show_help = true;
                break;
            case 'v':
                verbose++;
                break;
            case 'q':
                quiet = true;
                break;
            case '?':
                exit(EXIT_FAILURE);
            default:
                continue;
        }
    }

    if (optind == argc) {
        if (!show_help)
            fprintf(stderr, "Error: not enough arguments\n");
        help(program_invocation_short_name);
        exit(EXIT_FAILURE);
    }

    if (quiet) {
        log_set_level(level_none);
    } else if ((level_info + verbose) <= level_trace) {
        log_set_level(level_info + verbose);
    } else {
        log_set_level(level_trace);
    }

    while (cmd->fn) {
        if (!strcmp(cmd->name, argv[optind])) {
            int offset = optind;

            /* probe uses getopt, but for subcommands not using getopt */
            if (strcmp("probe", argv[optind])) {
                offset += 1;
            }
            optind = 1;

            return cmd->fn(program_invocation_short_name, argc - offset, argv + offset);
        }

        cmd++;
    }

    fprintf(stderr, "Error: unknown command: %s\n", argv[1]);
    exit(EXIT_FAILURE);
}
