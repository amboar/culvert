// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

/* For program_invocation_short_name */
#define _GNU_SOURCE
#include "arg_helper.h"
#include "ahb.h"
#include "ast.h"
#include "bridge/ilpc.h"
#include "priv.h"

#include <argp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cmd_ilpc_args {
    char *args[2];
};

static struct argp_option options[] = {
    {0}
};

/*
 * Similar to devmem, we use a single struct for all subcommands (read and write).
 * The overhead of having a separate struct for each subcommand is not worth it.
 */
static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    switch (key) {
        case ARGP_KEY_ARG:
            if (state->arg_num == 0) {
                if (strcmp("read", arg) && strcmp("write", arg))
                    argp_usage(state);
            }
            break;
        case ARGP_KEY_END:
            if (state->argc < 3)
                argp_usage(state);

            if (!strcmp("write", state->argv[1])) {
                if (state->argc != 4)
                    argp_usage(state);
            }
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp argp_ilpc = {
    options,
    parse_opt,
    "read/write ADDRESS [VALUE]",
    "ILPC command",
    NULL,
    NULL,
    NULL
};

int cmd_ilpc(struct argp_state* state)
{
    struct ilpcb _ilpcb, *ilpcb = &_ilpcb;
    struct ahb *ahb;
    int cleanup;
    int rc;

    struct subcommand ilpc_cmd;
    struct cmd_ilpc_args arguments = {0};
    parse_subcommand(&argp_ilpc, "ilpc", &arguments, state, &ilpc_cmd);

    rc = ilpcb_init(ilpcb);
    if (rc < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(program_invocation_short_name);
        } else {
            errno = -rc;
            perror("ilpcb_init");
        }
        exit(EXIT_FAILURE);
    }

    ahb = ilpcb_as_ahb(ilpcb);
    /* FIXME: argc/argv stuff once everything is migrated */
    rc = ast_ahb_access(NULL, ilpc_cmd.argc - 1, ilpc_cmd.argv + 1, ahb);
    if (rc) {
        errno = -rc;
        perror("ast_ahb_access");
        exit(EXIT_FAILURE);
    }

    cleanup = ilpcb_destroy(ilpcb);
    if (cleanup) {
        errno = -cleanup;
        perror("ilpcb_destroy");
        exit(EXIT_FAILURE);
    }

    return 0;
}
