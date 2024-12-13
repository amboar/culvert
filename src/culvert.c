// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
// Copyright (C) 2021, Oracle and/or its affiliates.

#include <errno.h>
#include <argp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "compiler.h"
#include "log.h"
#include "version.h"
#include "ahb.h"
#include "host.h"

int cmd_console(struct argp_state *state);
int cmd_coprocessor(struct argp_state *state);
int cmd_debug(struct argp_state *state);
int cmd_devmem(struct argp_state *state);
int cmd_ilpc(struct argp_state *state);
int cmd_jtag(struct argp_state *state);
int cmd_p2a(struct argp_state *state);
int cmd_probe(struct argp_state *state);
int cmd_otp(struct argp_state *state);
int cmd_read(struct argp_state *state);
int cmd_replace(struct argp_state *state);
int cmd_reset(struct argp_state *state);
int cmd_sfc(struct argp_state *state);
int cmd_trace(struct argp_state *state);
int cmd_write(struct argp_state *state);

const char *argp_program_version = "culvert " CULVERT_VERSION;
const char *argp_program_bug_address = "GitHub amboar/culvert";
static char doc[] =
    "\n"
    "Culvert -- A Test and Debug Tool for BMC AHB Interfaces"
    "\v"
    "Supported commands:\n"
    "   console     Start a getty on the BMC console\n"
    "   coprocessor Run stuff on your copressor\n"
    "   debug       Read or write data via debug UART\n"
    "   devmem      Use /dev/mem stuff\n"
    "   ilpc        Read or write data via iLPC\n"
    "   jtag        Start a remote-bitbang JTAG adapter for OpenOCD\n"
    "   otp         Read or write data via OTP\n"
    "   p2a         Read or write data via P2A\n"
    "   probe       Probe the BMC\n"
    "   read        Read the firmware or a memory address\n"
    "   replace     Replace matching content in the memory\n"
    "   reset       Reset a compoment via watchdog\n"
    "   sfc         Read, write or erase data on the FMC via SFC\n"
    "   trace       Trace an address on the BMC\n"
    "   write       Write firmware to the SPI or a memory address\n";

static struct argp_option options[] = {
    { "verbose", 'v', 0, 0, "Get verbose output", 0 },
    { "quiet", 'q', 0, 0, "Don't produce any output", 0 },
    { "skip-bridge", 's', "BRIDGE", 0, "Skip BRIDGE driver", 0 },
    { "list-bridges", 'l', 0, 0, "List available bridge drivers", 0 },
    {0}
};

struct arguments {
    char *args[2];
};

struct command {
    const char *name;
    int (*fn)(struct argp_state *);
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
    { "jtag", cmd_jtag },
    { "devmem", cmd_devmem },
    { "sfc", cmd_sfc },
    { "otp", cmd_otp },
    { "trace", cmd_trace },
    { "coprocessor", cmd_coprocessor },
    { NULL, NULL },
};

static error_t
parse_opt(int key, char *arg, struct argp_state *state)
{
    struct arguments *arguments = state->input;

    switch (key) {
        case 'q':
            log_set_level(level_none);
            break;
        case 'v':
            log_set_level(level_trace);
            break;
        case 's':
            if (disable_bridge_driver(arg)) {
                fprintf(stderr, "Error: '%s' not a recognized bridge name (use '-l' to list)\n", arg);
                exit(EXIT_FAILURE);
            }
            break;
        case 'l':
            print_bridge_drivers();
            exit(EXIT_SUCCESS);
            break;
        case ARGP_KEY_ARGS:
            for (int i = 0; i < state->argc - state->next; i++) {
                arguments->args[i] = state->argv[state->next + i];
            }
            for (const struct command *cmd = cmds; cmd->name; cmd++) {
                if (!strcmp(cmd->name, arguments->args[0])) {
                    // Remove arguments->args[0] from the list
                    for (int i = 0; i < state->argc - state->next; i++) {
                        state->argv[state->next + i] = state->argv[state->next + i + 1];
                    }
                    state->argc--;

                    int rc = cmd->fn(state);
                    exit(rc ? EXIT_FAILURE : EXIT_SUCCESS);
                }
            }
            argp_usage(state);
            break;
        case ARGP_KEY_END:
            if (state->arg_num < 1)
                argp_usage(state);
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp argp = {
    options,
    parse_opt,
    "<cmd> [CMD_OPTIONS]...",
    doc,
    NULL,
    NULL,
    NULL
};

int main(int argc, char *argv[])
{
    struct arguments arguments = {0};

    /*
     * Always initialise the log level and set a
     * different level if an argument is passed.
     */
    log_set_level(level_info);

    /*
     * Parse command-line arguments
     * NOTE: `ARGP_IN_ORDER` is required to make sub-commands work
     * properly and to enforce options to be in order.
    */
    if (argp_parse(&argp, argc, argv, ARGP_IN_ORDER, 0, &arguments) != 0) {
        fprintf(stderr, "Error parsing arguments\n");
        exit(EXIT_FAILURE);
    }

    exit(EXIT_FAILURE);
}
