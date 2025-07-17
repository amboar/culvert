/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2024 Andrew Jeffery */

#ifndef CULVERT_CMD_H
#define CULVERT_CMD_H

#include <argp.h>
#include <stdbool.h>
#include <string.h>

#include "connection.h"
#include "log.h"

#include "ccan/autodata/autodata.h"

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
 * 1. interface
 * 2. ip
 * 3. port
 * 4. username
 * 5. password
 *
 * @param argi The index of the `via` word in the argv array.
 * @param state The argp state.
 * @param args The connection arguments struct that should be modified.
 */
static inline int cmd_parse_via(int argi, struct argp_state *state,
                                struct connection_args *args)
{
    logt("via parse: argi = %d, state->argc = %d\n", argi, state->argc);
    if (argi >= state->argc) {
        loge("Provided argi >= argp state count. This is liekly a bug!\n");
        return -EINVAL;
    }

    /* global argc - already processed arguments - 1 (for the `via` word) */
    int argc = state->argc - argi - 1;

    /* Preflight validation if argc is either 1 or 5 */
    if (argc != 1 && argc != 5) {
        loge("via arguments must be either 1 or 5\n");
        return -EINVAL;
    }

    args->interface = state->argv[argi + 1];

    /* Early abort if interface is only set */
    if (argc == 1)
        return 0;

    logt("via parse: Detected more than one argument!\n");
    args->ip = state->argv[argi + 2];
    args->port = strtoul(state->argv[argi + 3], NULL, 0);
    args->username = state->argv[argi + 4];
    args->password = state->argv[argi + 5];
    args->internet_args = true;

    return 0;
}

#endif
