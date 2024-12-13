// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024 Tan Siewert

#include <argp.h>

#ifndef _CONNECTION_H
#define _CONNECTION_H

/*
 * Common struct that can be used in subcommands to pass connection arguments.
 * Commands that use this struct should use connection_parse_arguments() to
 * parse the arguments.
 */
struct connection_args
{
    const char *interface;
    const char *ip;
    const char *username;
    const char *password;
    int port;
};

/*
 * Parse connection arguments from the command line.
 * These options should be merged with the options of the subcommand if it is
 * required.
 */
static struct argp_option connection_options[] = {
    {"interface", 'i', "INTERFACE", 0, "Interface to connect to", 2},
    {"host", 'H', "HOST", 0, "Address to connect to", 2},
    {"port", 'p', "PORT", 0, "Port to connect to", 2},
    {"username", 'U', "USERNAME", 0, "Username to use for connection", 2},
    {"password", 'P', "PASSWORD", 0, "Password to use for connection", 2},
    // No {0} because it'll be used in the subcommand
};

/*
 * Parse connection arguments from the command line.
 * This function should be called in the subcommand's parse_opt function.
 */
static error_t
connection_parse_arguments(int key, char *arg, struct argp_state *state,
                           struct connection_args *arguments);
#endif
