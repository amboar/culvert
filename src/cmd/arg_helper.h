// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024 Tan Siewert

#ifndef _ARG_HELPER_H
#define _ARG_HELPER_H

#include <argp.h>

struct subcommand {
    const char *name;
    int argc;
    char **argv;
};
int parse_subcommand(const struct argp* argp, char* name, void* arguments, 
                     struct argp_state* state, struct subcommand* subcommand);
#endif