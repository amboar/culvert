// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

#include "ahb.h"
#include "ast.h"
#include "bridge/ilpc.h"
#include "cmd.h"
#include "priv.h"

#include <argp.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static char cmd_ilpc_args_doc[] = "<read ADDRESS|write ADDRESS LENGTH>";
static char cmd_ilpc_doc[] =
    "\n"
    "ilpc command"
    "\v"
    "Supported commands:\n"
    "  read        Read data via iLPC\n"
    "  write       Write data via iLPC\n";

static struct argp_option cmd_ilpc_options[] = {
    {0},
};

struct cmd_ilpc_args {
    struct ast_ahb_args ahb_args;
};

static error_t cmd_ilpc_parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_ilpc_args *arguments = state->input;

    switch (key) {
    case ARGP_KEY_ARG:
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
       break;
   default:
       return ARGP_ERR_UNKNOWN;
   }

    return 0;
}

static struct argp cmd_ilpc_argp = {
    .options = cmd_ilpc_options,
    .parser = cmd_ilpc_parse_opt,
    .args_doc = cmd_ilpc_args_doc,
    .doc = cmd_ilpc_doc,
};

static int do_ilpc(int argc, char **argv)
{
    struct ilpcb _ilpcb , *ilpcb = &_ilpcb;
    struct ahb *ahb;
    int cleanup, rc;

    struct cmd_ilpc_args arguments = {0};
    rc = argp_parse(&cmd_ilpc_argp, argc, argv, ARGP_IN_ORDER, 0, &arguments);
    if (rc != 0)
        return rc;

    /*
     * We do not use debug_drvier_probe here as we need data
     * that's not being returned there.
     */
    rc = ilpcb_init(ilpcb);
    if (rc < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(program_invocation_short_name);
        } else {
            errno = -rc;
            perror("ilpcb_init");
        }
        return rc;
    }

    ahb = ilpcb_as_ahb(ilpcb);
    rc = ast_ahb_access(&arguments.ahb_args, ahb);
    if (rc) {
        errno = -rc;
        perror("ast_ahb_access");
        return rc;
    }

    cleanup = ilpcb_destroy(ilpcb);
    if (cleanup) {
        errno = -cleanup;
        perror("ilpcb_destroy");
        return rc;
    }

    return 0;
}

static const struct cmd ilpc_cmd = {
    .name = "ilpc",
    .description = "Read or write via iLPC",
    .fn = do_ilpc,
};
REGISTER_CMD(ilpc_cmd);
