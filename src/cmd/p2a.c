// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

/* For program_invocation_short_name */
#define _GNU_SOURCE

#include "arg_helper.h"
#include "ahb.h"
#include "ast.h"
#include "bridge/p2a.h"
#include "log.h"
#include "priv.h"

#include <argp.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char doc[] =
    "\n"
    "P2A command"
    "\v"
    "Supported operations:\n"
    "  read        Read data via P2A\n"
    "  write       Write data via P2A\n\n"
    "Supported device types:\n"
    "  vga         VGA device\n"
    "  bmc         BMC device\n\n"
    "For read operation, the address is required.\n"
    "For write operation, the address and value are required.\n"
    "Example:\n\n"
    "  culvert p2a -t vga read 0x100000\n"
    "  culvert p2a -t bmc write 0x100000 0x12345678\n";

struct cmd_p2a_args
{
    const char *io_operation;
    const char *address;
    const char *value;
    uint16_t type;
};

static struct argp_option options[] = {
    {"type", 't', "TYPE", 0, "P2A device type", 0},
    {0},
};

static error_t
parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_p2a_args *arguments = state->input;

    switch (key)
    {
    case 't':
        if (!strcmp("vga", arg))
            arguments->type = AST_PCI_DID_VGA;
        else if (!strcmp("bmc", arg))
            arguments->type = AST_PCI_DID_BMC;
        else
            argp_error(state, "Unknown device type: %s", arg);
        break;
    case ARGP_KEY_ARG:
        switch (state->arg_num)
        {
        case 0:
            if (strcmp("read", arg) && strcmp("write", arg))
                argp_error(state, "Unknown operation: %s", arg);
            arguments->io_operation = arg;
            break;
        case 1:
            arguments->address = arg;
            break;
        case 2:
            arguments->value = arg;
            break;
        default:
            argp_usage(state);
        }
        break;
    case ARGP_KEY_END:
        if (state->arg_num < 2)
            argp_error(state, "Missing arguments");
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

struct argp argp = {
    options,
    parse_opt,
    "read|write ADDRESS [VALUE]",
    doc,
    NULL,
    NULL,
    NULL,
};

int cmd_p2a(struct argp_state *state)
{
    struct p2ab _p2ab, *p2ab = &_p2ab;
    struct ahb *ahb;
    int cleanup;
    int rc;

    struct subcommand p2a_cmd;
    struct cmd_p2a_args arguments = {0};
    parse_subcommand(&argp, "p2a", &arguments, state, &p2a_cmd);

    rc = p2ab_init(p2ab, AST_PCI_VID, arguments.type);

    if (rc < 0)
    {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root())
            priv_print_unprivileged(program_invocation_short_name);
        else
        {
            errno = -rc;
            perror("p2ab_init");
        }
        exit(EXIT_FAILURE);
    }

    /* FIXME: Once all commands are migrated this should be made better */
    int argc = arguments.value ? 3 : 2;
    char *argv[] = {
        (char *)arguments.io_operation,
        (char *)arguments.address,
    };

    if (arguments.value)
        argv[2] = (char *)arguments.value;

    ahb = p2ab_as_ahb(p2ab);
    rc = ast_ahb_access(NULL, argc, argv, ahb);
    if (rc)
    {
        errno = -rc;
        perror("ast_ahb_access");
        exit(EXIT_FAILURE);
    }

    cleanup = p2ab_destroy(p2ab);
    if (cleanup)
    {
        errno = -cleanup;
        perror("p2ab_destroy");
        exit(EXIT_FAILURE);
    }

    return 0;
}
