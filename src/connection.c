// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024 Tan Siewert

#include "connection.h"

#include <argp.h>

static error_t
connection_parse_arguments(int key, char *arg, struct argp_state *state,
                           struct connection_args *arguments)
{
    switch (key)
    {
    case 'i':
        arguments->interface = arg;
        break;
    case 'H':
        arguments->ip = arg;
        break;
    case 'p':
        arguments->port = atoi(arg);
        break;
    case 'U':
        arguments->username = arg;
        break;
    case 'P':
        arguments->password = arg;
        break;
    }

    return 0;
}
