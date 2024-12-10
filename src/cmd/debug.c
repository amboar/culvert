// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

/* For program_invocation_short_name */
#define _GNU_SOURCE

#include "ahb.h"
#include "arg_helper.h"
#include "ast.h"
#include "bridge/debug.h"
#include "log.h"

#include <argp.h>
#include <string.h>
#include <stdlib.h>

static char global_doc[] =
    "\n"
    "Debug command"
    "\v"
    "Supported commands:\n"
    "  read        Read data via debug UART\n"
    "  write       Write data via debug UART\n";

struct cmd_debug_args {
    const char *address;
    const char *interface;
    const char *ip;
    int port;
    const char *username;
    const char *password;
    /* Only for write. Should be at the end for memory alignment. */
    const char *value;
};

static struct argp_option options[] = {
    {0}
};

static error_t parse_opt_read(int key, char *arg, struct argp_state *state)
{
    struct cmd_debug_args *arguments = state->input;

    switch (key) {
        case ARGP_KEY_ARG:
            switch (state->arg_num) {
                case 0:
                    arguments->address = arg;
                    break;
                case 1:
                    arguments->interface = arg;
                    break;
                case 2:
                    arguments->ip = arg;
                    break;
                case 3:
                    arguments->port = atoi(arg);
                    break;
                case 4:
                    arguments->username = arg;
                    break;
                case 5:
                    arguments->password = arg;
                    break;
                default:
                    argp_usage(state);
            }
            break;
        case ARGP_KEY_END:
            if (state->argc != 3 && state->argc != 7)
                argp_usage(state);
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp argp_read = {
    options,
    parse_opt_read,
    "ADDRESS INTERFACE [IP PORT USERNAME PASSWORD]",
    "Read data via debug UART",
    NULL,
    NULL,
    NULL
};

int cmd_debug_read(struct argp_state* state)
{
    struct debug _debug, *debug = &_debug;
    struct ahb *ahb;
    int rc;
    int cleanup;

    /* ./culvert debug read 0x1e6e207c digi,portserver-ts-16 <IP> <SERIAL PORT> <USER> <PASSWORD> */
    struct subcommand debug_cmd;
    struct cmd_debug_args arguments = {0};
    parse_subcommand(&argp_read, "read", &arguments, state, &debug_cmd);

    logi("Initialising debug interface\n");
    if (arguments.ip != NULL)
        rc = debug_init(debug, arguments.interface, arguments.ip, arguments.port,
                        arguments.username, arguments.password);
    else
        rc = debug_init(debug, arguments.interface);
    
    if (rc < 0) {
        errno = -rc;
        perror("debug_init");
        exit(EXIT_FAILURE);
    }

    rc = debug_enter(debug);
    if (rc < 0) { errno = -rc; perror("debug_enter"); goto cleanup_debug; }

    ahb = debug_as_ahb(debug);
    /* FIXME: Make read/write determination better */
    debug_cmd.argv[0] = "read";
    rc = ast_ahb_access(program_invocation_short_name, debug_cmd.argc,
                        debug_cmd.argv, ahb);
    if (rc) {
        errno = -rc;
        perror("ast_ahb_access");
        exit(EXIT_FAILURE);
    }

    cleanup = debug_exit(debug);
    if (cleanup < 0) { errno = -cleanup; perror("debug_exit"); }

cleanup_debug:
    logi("Destroying debug interface\n");
    cleanup = debug_destroy(debug);
    if (cleanup < 0) { errno = -cleanup; perror("debug_destroy"); }

    return rc;
}

static error_t parse_opt_write(int key, char *arg, struct argp_state *state)
{
    struct cmd_debug_args *arguments = state->input;

    switch (key) {
        case ARGP_KEY_ARG:
            switch (state->arg_num) {
                case 0:
                    arguments->address = arg;
                    break;
                case 1:
                    arguments->value = arg;
                    break;
                case 2:
                    arguments->interface = arg;
                    break;
                case 3:
                    arguments->ip = arg;
                    break;
                case 4:
                    arguments->port = atoi(arg);
                    break;
                case 5:
                    arguments->username = arg;
                    break;
                case 6:
                    arguments->password = arg;
                    break;
                default:
                    argp_usage(state);
            }
            break;
        case ARGP_KEY_END:
            if (state->argc != 4 && state->argc != 8)
                argp_usage(state);
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp argp_write = {
    options,
    parse_opt_write,
    "ADDRESS VALUE INTERFACE [IP PORT USERNAME PASSWORD]",
    "Write data via debug UART",
    NULL,
    NULL,
    NULL
};

int cmd_debug_write(struct argp_state* state)
{
    struct debug _debug, *debug = &_debug;
    struct ahb *ahb;
    int rc;
    int cleanup;

    struct subcommand debug_cmd;
    struct cmd_debug_args arguments = {0};
    parse_subcommand(&argp_write, "write", &arguments, state, &debug_cmd);

    logi("Initialising debug interface\n");
    if (arguments.ip != NULL)
        rc = debug_init(debug, arguments.interface, arguments.ip, arguments.port,
                        arguments.username, arguments.password);
    else
        rc = debug_init(debug, arguments.interface);

    if (rc < 0) {
        errno = -rc;
        perror("debug_init");
        exit(EXIT_FAILURE);
    }

    rc = debug_enter(debug);
    if (rc < 0) { errno = -rc; perror("debug_enter"); goto cleanup_debug; }

    ahb = debug_as_ahb(debug);
    /* FIXME: Make read/write determination better */
    debug_cmd.argv[0] = "write";
    rc = ast_ahb_access(program_invocation_short_name, debug_cmd.argc,
                        debug_cmd.argv, ahb);
    if (rc) {
        errno = -rc;
        perror("ast_ahb_access");
        exit(EXIT_FAILURE);
    }

    cleanup = debug_exit(debug);
    if (cleanup < 0) { errno = -cleanup; perror("debug_exit"); }

cleanup_debug:
    logi("Destroying debug interface\n");
    cleanup = debug_destroy(debug);
    if (cleanup < 0) { errno = -cleanup; perror("debug_destroy"); }

    return rc;
}

static error_t parse_opt_global(int key, char *arg, struct argp_state *state)
{
    switch (key) {
        case ARGP_KEY_ARG:
            if (state->arg_num == 0 && !strcmp(arg, "read")) {
                cmd_debug_read(state);
            } else if (state->arg_num == 0 && !strcmp(arg, "write")) {
                cmd_debug_write(state);
            }
            break;
        case ARGP_KEY_END:
            if (state->arg_num < 1) {
                argp_usage(state);
            }
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp_global = {
    options,
    parse_opt_global,
    "<command>",
    global_doc,
    NULL,
    NULL,
    NULL
};

int cmd_debug(struct argp_state* state)
{
    struct subcommand debug_cmd;
    struct cmd_debug_args arguments = {0};
    parse_subcommand(&argp_global, "debug", &arguments, state, &debug_cmd);

    return EXIT_FAILURE;
}
