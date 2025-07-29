/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2024 Andrew Jeffery */

#ifndef CULVERT_CMD_H
#define CULVERT_CMD_H

#include "host.h"
#include "connection.h"
#include "log.h"

#include "ccan/autodata/autodata.h"

#include <argp.h>
#include <stdbool.h>
#include <string.h>

struct cmd {
    const char *name;
    const char *description;
    int (*fn)(int argc, char **argv);
};

AUTODATA_TYPE(cmds, struct cmd);
#define REGISTER_CMD(cmd) AUTODATA_SYM(cmds, cmd);

static inline int cmd_cmp(const void *a, const void *b)
{
    const struct cmd * const *acmd = a;
    const struct cmd * const *bcmd = b;

    return strcmp((*acmd)->name, (*bcmd)->name);
}

/**
 * Parse everything after the `via` word.
 * The `via` word itself is not included in the arguments.
 * The arguments are expected to be in the following order:
 * 1. bridge driver
 * 2. interface
 * 3. ip
 * 4. port
 * 5. username
 * 6. password
 *
 * @param argi The index of the `via` word in the argv array.
 * @param state The argp state.
 * @param args The connection arguments struct that should be modified.
 */
static inline int cmd_parse_via(int argi, struct argp_state *state,
                                struct connection_args *args)
{
    int rc;
    logt("via parse: argi = %d, state->argc = %d\n", argi, state->argc);
    if (argi >= state->argc) {
        loge("Provided argi >= argp state count. This is liekly a bug!\n");
        return -EINVAL;
    }

    /* global argc - already processed arguments - 1 (for the `via` word) */
    int argc = state->argc - argi - 1;

    /* Preflight validation if argc is either 1, 2 or 5 */
    if (argc != 1 && argc != 2 && argc != 6) {
        loge("via arguments must be either 1, 2 or 5\n");
        return -EINVAL;
    }

    /* Resolve bridge driver to check if it even exists and bind it */
    rc = get_bridge_driver(state->argv[argi + 1], &args->bridge_driver);
    if (rc != 0) {
        loge("Couldn't find provided bridge driver '%s'\n", state->argv[argi + 1]);
        return rc;
    }

    /* Early abort if driver is only set, but fail when path_required is true */
    if (argc == 1) {
        if (args->bridge_driver->path_required) {
            loge("Bridge driver '%s' requires an interface path\n", args->bridge_driver->name);
            return -EINVAL;
        }
        return 0;
    }

    args->interface = state->argv[argi + 2];

    /* Early abort if interface is set */
    if (argc == 2)
        return 0;

    logt("via parse: Detected more than one argument!\n");
    args->ip = state->argv[argi + 3];
    args->port = strtoul(state->argv[argi + 4], NULL, 0);
    args->username = state->argv[argi + 5];
    args->password = state->argv[argi + 6];
    args->internet_args = true;

    return 0;
}

#endif
