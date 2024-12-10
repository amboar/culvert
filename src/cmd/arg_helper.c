// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024 Tan Siewert

#include "arg_helper.h"

#include <argp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int parse_subcommand(const struct argp* argp, char* name, void *arguments,
                     struct argp_state* state, struct subcommand* subcommand)
{
    int argc = state->argc - state->next + 1;
    char **argv = &state->argv[state->next - 1];

    argv[0] = malloc(strlen(state->name) + strlen(name) + 1);
    if (!argv[0])
        argp_failure(state, 1, ENOMEM, 0);

    sprintf(argv[0], "%s %s", state->name, name);

    subcommand->name = name;
    subcommand->argc = argc;
    subcommand->argv = argv;

    argp_parse(argp, argc, argv, ARGP_IN_ORDER, 0, arguments);

    return 0;
}