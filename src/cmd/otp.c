// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

#include "arg_helper.h"
#include "ahb.h"
#include "ast.h"
#include "compiler.h"
#include "host.h"
#include "log.h"
#include "priv.h"
#include "soc/otp.h"

#include <argp.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char doc[] =
    "\n"
    "OTP command"
    "\v"
    "Supported commands:\n"
    "  read        Read OTP\n"
    "  write       Write OTP\n\n"
    "Supported regions:\n"
    "  conf        Configuration region\n"
    "  strap       Strap region\n\n"
    "When writing on the OTP, please note the following:\n"
    "  For strap, the bit and value are required.\n"
    "  For conf, the word and bit are required.\n";

struct cmd_otp_args {
    enum otp_region region;
    int key_arg_count;
    int read;
    unsigned int bit;
    unsigned int value;
};

static struct argp_option options[] = {
    {"region", 'r', "REGION", 0, "OTP region to access", 0},
    {0}
};

static error_t
parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_otp_args *arguments = state->input;

    if (key == ARGP_KEY_ARG)
        arguments->key_arg_count++;

    switch (key)
    {
    case 'r':
        if (!strcmp("conf", arg))
            arguments->region = otp_region_conf;
        else if (!strcmp("strap", arg))
            arguments->region = otp_region_strap;
        else
            argp_error(state, "Invalid region '%s'", arg);
        break;
    case ARGP_KEY_ARG:
        switch (state->arg_num)
        {
        case 0:
            if (strcmp("read", arg) && strcmp("write", arg))
                argp_error(state, "Invalid operation '%s'", arg);

            if (!strcmp("read", arg))
                arguments->read = 1;
            break;
        case 1:
            if (!arguments->read)
                arguments->bit = strtoul(arg, NULL, 0);
            break;
        case 2:
            if (!arguments->read)
                arguments->value = strtoul(arg, NULL, 0);
            break;
        }
        break;
    case ARGP_KEY_END:
        if (!arguments->region)
            argp_error(state, "No region defined");
        if (arguments->key_arg_count < 1)
            argp_error(state, "Missing operation");
        if (!arguments->read && (!arguments->bit || !arguments->value))
            argp_error(state, "Missing word/bit");
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp argp = {
    options,
    parse_opt,
    "read|write [BIT [VALUE|WORD]]",
    doc,
    NULL,
    NULL,
    NULL
};

int cmd_otp(struct argp_state *state)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct otp *otp;
    struct ahb *ahb;
    int rc;

    struct subcommand otp_cmd;
    struct cmd_otp_args arguments = {0};
    parse_subcommand(&argp, "otp", &arguments, state, &otp_cmd);

    // hacky, only for migration
    int argc = otp_cmd.argc - 2;
    char **argv = otp_cmd.argv + 2;

    if (!arguments.read) {
        argc -= 2;
        argv += 2;
    }

    if ((rc = host_init(host, argc, argv)) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        exit(EXIT_FAILURE);
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = EXIT_FAILURE;
        goto cleanup_host;
    }

    if ((rc = soc_probe(soc, ahb)) < 0) {
        errno = -rc;
        perror("soc_probe");
        goto cleanup_host;
    }

    if (!(otp = otp_get(soc))) {
        loge("Failed to acquire OTP controller, exiting\n");
        rc = EXIT_FAILURE;
        goto cleanup_soc;
    }

    if (arguments.read)
        rc = otp_read(otp, arguments.region);
    else {
        if (arguments.region == otp_region_strap)
            rc = otp_write_strap(otp, arguments.bit, arguments.value);
        else
            rc = otp_write_conf(otp, arguments.value, arguments.bit);
    }

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    return rc;
}
