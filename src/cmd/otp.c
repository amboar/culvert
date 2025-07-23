// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

#include "ahb.h"
#include "ast.h"
#include "cmd.h"
#include "compiler.h"
#include "connection.h"
#include "host.h"
#include "log.h"
#include "priv.h"
#include "soc/otp.h"

#include <argp.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char cmd_otp_args_doc[] =
    "<conf|strap> <read|write <WORD BIT|BIT VALUE>>"
    " [via DRIVER [INTERFACE [IP PORT USERNAME PASSWORD]]]";

static char cmd_otp_doc[] =
    "\n"
    "Otp command"
    "\v"
    "Supported regions:\n"
    "  conf        OTP configuration (word and bit for write)\n"
    "  strap       OTP strap (bit and value for write)"
    "\v"
    "Supported commands:\n"
    "  read        Read data to OTP storage\n"
    "  write       Write data to OTP storage\n";

static struct argp_option cmd_otp_options[] = {
    {0},
};

struct cmd_otp_args {
    struct connection_args connection;
    enum otp_region region;
    bool read;
    unsigned int key;
    unsigned int value;
};

static error_t cmd_otp_parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_otp_args *arguments = state->input;
    int rc;

    switch (key) {
    case ARGP_KEY_ARG:
        if (!strcmp(arg, "via")) {
            rc = cmd_parse_via(state->next - 1, state, &arguments->connection);
            if (rc != 0)
                argp_error(state, "Failed to parse connection arguments. Returned code %d", rc);
            break;
        }

        switch (state->arg_num) {
        case 0:
            if (strcmp("conf", arg) && strcmp("strap", arg))
                argp_error(state, "Invalid region '%s'", arg);

            arguments->region = !strcmp("conf", arg) ? otp_region_conf : otp_region_strap;
            break;
        case 1:
            if (strcmp("read", arg) && strcmp("write", arg))
                argp_error(state, "Invalid command '%s'", arg);

            arguments->read = !(strcmp(arg, "read"));
            break;
        case 2:
            arguments->key = strtoul(arg, NULL, 0);
            break;
        case 3:
            arguments->key = strtoul(arg, NULL, 0);
            break;
        }
        break;
    case ARGP_KEY_END:
        if (state->arg_num < 2)
            argp_error(state, "Not enough arguments");
        if (!arguments->read && state->arg_num < 4)
            argp_error(state, "Not enough arguments for write");
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp cmd_otp_argp = {
    .options = cmd_otp_options,
    .parser = cmd_otp_parse_opt,
    .args_doc = cmd_otp_args_doc,
    .doc = cmd_otp_doc,
};

static int do_otp(int argc, char **argv)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct otp *otp;
    struct ahb *ahb;
    int rc;

    struct cmd_otp_args arguments = {0};
    rc = argp_parse(&cmd_otp_argp, argc, argv, ARGP_IN_ORDER, 0, &arguments);
    if (rc != 0)
        return rc;

    if ((rc = host_init(host, &arguments.connection)) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        return rc;
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

    if (soc_generation(soc) != ast_g6) {
        loge("We currently only support the AST2600-series coprocessor\n");
        rc = EXIT_FAILURE;
        goto cleanup_soc;
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
            rc = otp_write_strap(otp, arguments.key, arguments.value);
        else
            rc = otp_write_conf(otp, arguments.key, arguments.value);
    }

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    return rc;
}

static const struct cmd otp_cmd = {
    .name = "otp",
    .description = "Read and write the OTP configuration (AST2600-only)",
    .fn = do_otp,
};
REGISTER_CMD(otp_cmd);
