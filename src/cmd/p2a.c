// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

#include "ahb.h"
#include "ast.h"
#include "bridge/p2a.h"
#include "cmd.h"
#include "log.h"
#include "priv.h"

#include <argp.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char cmd_p2a_args_doc[] = "<vga|bmc> <read ADDRESS|write ADDRESS LENGTH>";
static char cmd_p2a_doc[] =
    "\n"
    "p2a command"
    "\v"
    "Supported PCIe devices:\n"
    "  vga         VGA PCIe device\n"
    "  bmc         BMC PCIe device\n"
    "\v"
    "Supported commands:\n"
    "  read        Read data via p2a\n"
    "  write       Write data via p2a\n";

static struct argp_option cmd_p2a_options[] = {
    {0},
};

struct cmd_p2a_args {
    struct ast_ahb_args ahb_args;
    uint16_t device_id;
};

static error_t cmd_p2a_parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_p2a_args *arguments = state->input;

    switch (key) {
    case ARGP_KEY_ARG:
        switch (state->arg_num) {
        case 0:
            if (!strcmp("vga", arg))
                arguments->device_id = AST_PCI_DID_VGA;
            else if (!strcmp("bmc", arg))
                arguments->device_id = AST_PCI_DID_BMC;
            else
                argp_error(state, "Invalid PCIe device '%s'", arg);
            break;
        case 1:
            if (strcmp("read", arg) && strcmp("write", arg))
                argp_error(state, "Invalid operation '%s'", arg);

            if (!strcmp("read", arg))
                arguments->ahb_args.read = true;
            break;
        case 2:
            arguments->ahb_args.address = strtoul(arg, NULL, 0);
            break;
        case 3:
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
       if (state->arg_num < 3)
           argp_error(state, "Not enough arguments provided. Need the device type, the operation and an address.");

       if (!arguments->ahb_args.read && state->arg_num < 4)
           argp_error(state, "Write mode detected but either no address or value detected");
       break;
   default:
       return ARGP_ERR_UNKNOWN;
   }

    return 0;
}

static struct argp cmd_p2a_argp = {
    .options = cmd_p2a_options,
    .parser = cmd_p2a_parse_opt,
    .args_doc = cmd_p2a_args_doc,
    .doc = cmd_p2a_doc,
};

static int do_p2a(int argc, char **argv)
{
    struct p2ab _p2ab, *p2ab = &_p2ab;
    struct ahb *ahb;
    int cleanup, rc;

    struct cmd_p2a_args arguments = {0};
    rc = argp_parse(&cmd_p2a_argp, argc, argv, ARGP_IN_ORDER, 0, &arguments);
    if (rc != 0)
        return rc;

    rc = p2ab_init(p2ab, AST_PCI_VID, arguments.device_id);
    if (rc < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(program_invocation_short_name);
        } else {
            errno = -rc;
            perror("p2ab_init");
        }
        return rc;
    }

    ahb = p2ab_as_ahb(p2ab);
    rc = ast_ahb_access(&arguments.ahb_args, ahb);
    if (rc) {
        errno = -rc;
        perror("ast_ahb_access");
        return rc;
    }

    cleanup = p2ab_destroy(p2ab);
    if (cleanup) {
        errno = -cleanup;
        perror("p2ab_destroy");
        return cleanup;
    }

    return 0;
}

static const struct cmd p2a_cmd = {
    .name = "p2a",
    .description = "Read or write data via p2a devices",
    .fn = do_p2a,
};
REGISTER_CMD(p2a_cmd);
