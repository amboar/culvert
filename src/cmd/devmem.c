// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

/* For program_invocation_short_name */
#define _GNU_SOURCE

#include <argp.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ahb.h"
#include "arg_helper.h"
#include "ast.h"
#include "bridge/devmem.h"
#include "priv.h"

struct cmd_devmem_args {
    char *args[2];
};

static struct argp_option options[] = {
    {0}
};

/*
 * Instead of having a separate struct for each subcommand, we use a single
 * struct for all subcommands (read and write). This is because the actual
 * handling for reading and writing is done in ast_ahb_access, where it checks
 * if argv[0] is the operation to be performed.
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
            if (state->argc < 2)
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

static struct argp argp_devmem = {
    options,
    parse_opt,
    "read|write ADDRESS [VALUE]",
    "/dev/mem stuff",
    NULL,
    NULL,
    NULL
};

int cmd_devmem(struct argp_state* state)
{
    struct devmem _devmem, *devmem = &_devmem;
    struct ahb *ahb;
    int cleanup;
    int rc;

    struct subcommand devmem_cmd;
    struct cmd_devmem_args arguments = {0};
    parse_subcommand(&argp_devmem, "devmem", &arguments, state, &devmem_cmd);

    rc = devmem_init(devmem);
    if (rc < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(program_invocation_short_name);
        } else {
            errno = -rc;
            perror("devmem_init");
        }
        exit(EXIT_FAILURE);
    }

    ahb = devmem_as_ahb(devmem);
    /* FIXME: argc + argv once all commands are migrated */
    rc = ast_ahb_access(program_invocation_short_name,
                        devmem_cmd.argc - 1, devmem_cmd.argv + 1, ahb);
    if (rc) {
        errno = -rc;
        perror("ast_ahb_access");
        exit(EXIT_FAILURE);
    }

    cleanup = devmem_destroy(devmem);
    if (cleanup) {
        errno = -cleanup;
        perror("devmem_destroy");
        exit(EXIT_FAILURE);
    }

    return 0;
}

