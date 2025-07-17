// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Tan Siewert

#ifndef _CONNECTION_H
#define _CONNECTION_H

/**
 * Common struct that can be used in subcommands to pass connection arguments.
 * Commands that use this struct should use cmd_parse_via() to parse the arguments.
 *
 * The so-called "internet args" like IP, username, password, and so on are,
 * as of now, only available if the remote is a Digi Portserver TS 16.
 * When the arguments are set, the `internet_args` flag must be true. Otherwise,
 * the bridge implementations will ignore the settings.
 * Usually, you do not have to set this, as it will be set by cmd_parse_via().
 * However, if you do have a command that only requires a sub-set of the values,
 * then you may have to set it on your own.
 */
struct connection_args {
    /** BMC interface path (e.g. /dev/ttyUSB0) */
    const char *interface;

    /** IP address of the console server */
    const char *ip;

    /** Username for connecting to the console server */
    const char *username;

    /** Password for connecting to the console server */
    const char *password;

    /** Port for connecting to the console server */
    int port;

    /**
     * Internal flag that must be true if console server fields
     * (IP, username, etc.) are set.
     */
    bool internet_args;
};
#endif
