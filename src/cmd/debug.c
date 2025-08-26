// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

#include "ahb.h"
#include "ast.h"
#include "bridge/debug.h"
#include "cmd.h"
#include "connection.h"
#include "host.h"
#include "log.h"

#include <argp.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

static char cmd_debug_args_doc[] = "<read ADDRESS|write ADDRESS LENGTH> via DRIVER INTERFACE [IP PORT USERNAME PASSWORD]";
static char cmd_debug_doc[] =
    "\n"
    "Debug command"
    "\v"
    "Supported commands:\n"
    "  read        Read data via debug UART\n"
    "  write       Write data via debug UART\n";

static struct argp_option cmd_debug_options[] = {
    { "force-quit", 'F', 0, 0, "Blindly exit debug mode before entering", 0 },
    {0},
};

struct cmd_debug_args {
    struct connection_args connection;
    struct ast_ahb_args ahb_args;
    bool force_quit;
};

static error_t cmd_debug_parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_debug_args *arguments = state->input;
    int rc;

    switch (key) {
    case 'F':
        arguments->force_quit = 1;
        break;
    case ARGP_KEY_ARG:
        if (!strcmp(arg, "via")) {
            rc = cmd_parse_via(state->next - 1, state, &arguments->connection);
            if (rc != 0)
                argp_error(state, "Failed to parse connection arguments. Returned code %d", rc);
            break;
        }

        switch (state->arg_num) {
        case 0:
            if (strcmp("read", arg) && strcmp("write", arg))
                argp_error(state, "Invalid command '%s'", arg);

            if (!strcmp("read", arg))
                arguments->ahb_args.read = true;
            break;
        case 1:
            arguments->ahb_args.address = strtoul(arg, NULL, 0);
            break;
        case 2:
            if (!arguments->ahb_args.read) {
                static uint32_t write_val;
                write_val = strtoul(arg, NULL, 0);
                arguments->ahb_args.write_value = &write_val;
            } else {
                logd("Found third argument in read mode, ignoring...\n");
            }
            break;
        }
       break;
   case ARGP_KEY_END:
       if (state->arg_num < 2)
           argp_error(state, "Not enough arguments provided. Need at least an address.");

       if (!arguments->ahb_args.read && state->arg_num < 3)
           argp_error(state, "Write mode detected but either no address or value detected");

       if (arguments->connection.interface == NULL)
           argp_error(state, "Debug interface path must be defined");
       break;
   default:
       return ARGP_ERR_UNKNOWN;
   }

    return 0;
}

static struct argp cmd_debug_argp = {
    .options = cmd_debug_options,
    .parser = cmd_debug_parse_opt,
    .args_doc = cmd_debug_args_doc,
    .doc = cmd_debug_doc,
};

static int do_debug(int argc, char **argv)
{
    struct debug _debug, *debug = &_debug;
    struct ahb *ahb;
    int rc, cleanup;

    /* ./culvert debug read 0x1e6e207c digi,portserver-ts-16 <IP> <SERIAL PORT> <USER> <PASSWORD> */
    struct cmd_debug_args arguments = {0};
    rc = argp_parse(&cmd_debug_argp, argc, argv, ARGP_IN_ORDER, 0, &arguments);
    if (rc != 0)
        return rc;

    /* Ensure that force_quit is passed to debug ctx */
    debug->force_quit = arguments.force_quit;

    logi("Initialising debug interface\n");
    if (!arguments.connection.internet_args) {
        /* Local debug interface */
        if ((rc = debug_init(debug, arguments.connection.interface)) < 0) {
            loge("Failed to initialise local debug interace on %s: %d\n",
                    arguments.connection.interface, rc);
            return rc;
        }
    } else {
        /* Remote debug interface */
        rc = debug_init(debug, arguments.connection.interface,
                        arguments.connection.ip, arguments.connection.port,
                        arguments.connection.username,
                        arguments.connection.password);
        if (rc < 0) {
            loge("Failed to initialise remote debug interface: %d\n", rc);
            return rc;
        }
    }

    if (rc < 0) {
        errno = -rc;
        perror("debug_init");
        return rc;
    }

    rc = debug_enter(debug);
    if (rc < 0) { errno = -rc; perror("debug_enter"); goto destroy_ctx; }

    ahb = debug_as_ahb(debug);
    rc = ast_ahb_access(&arguments.ahb_args, ahb);
    if (rc) {
        errno = -rc;
        perror("ast_ahb_access");
        return rc;
    }

    cleanup = debug_exit(debug);
    if (cleanup < 0) { errno = -cleanup; perror("debug_exit"); }

destroy_ctx:
    logi("Destroying debug interface\n");
    cleanup = debug_destroy(debug);
    if (cleanup < 0) { errno = -cleanup; perror("debug_destroy"); }

    return rc;
}

static const struct cmd debug_cmd = {
    .name = "debug",
    .description = "Read or write 4 bytes of data via the AHB bridge",
    .fn = do_debug,
};
REGISTER_CMD(debug_cmd);
