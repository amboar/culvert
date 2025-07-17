// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

#include "cmd.h"
#include "compiler.h"
#include "connection.h"
#include "host.h"
#include "log.h"
#include "soc.h"

#include <argp.h>
#include <stdio.h>

static char cmd_probe_args_doc[] = "[-l] [-r REQUIREMENT] [via DRIVER [INTERFACE [IP PORT USERNAME PASSWORD]]]";
static char cmd_probe_doc[] =
    "\n"
    "Probe command"
    "\v"
    "Supported requirements:\n"
    "  integrity        Require integrity\n"
    "  confidentiality  Require confidentiality\n";

static struct argp_option cmd_probe_options[] = {
    {"list-interfaces", 'l', 0, 0, "List available interfaces", 0},
    {"require", 'r', "REQUIREMENT", 0, "Requirement to probe for", 0},
    {0},
};

struct cmd_probe_args {
    int list_ifaces;
    enum bridge_mode requirement;
    struct connection_args connection;
};

static error_t cmd_probe_parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_probe_args *arguments = state->input;
    int rc = 0;

    switch (key) {
    case 'l':
        arguments->list_ifaces = 1;
        break;
    case 'r':
        if (strcmp("integrity", arg) && strcmp("confidentiality", arg))
            argp_error(state, "Invalid requirement '%s'", arg);

        if (!strcmp("integrity", arg))
            arguments->requirement = bm_restricted;
        else
            arguments->requirement = bm_disabled;
        break;
    case ARGP_KEY_ARG:
        if (!strcmp(arg, "via")) {
            rc = cmd_parse_via(state->next - 1, state, &arguments->connection);
            if (rc != 0)
                argp_error(state, "Failed to parse connection arguments. Returned code %d", rc);
            return 0;
        }

        break;
    case ARGP_KEY_END:
        if (!arguments->requirement)
            arguments->requirement = bm_permissive;

        if (!arguments->connection.interface)
            arguments->connection.interface = NULL;
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp cmd_probe_argp = {
    .options = cmd_probe_options,
    .parser = cmd_probe_parse_opt,
    .args_doc = cmd_probe_args_doc,
    .doc = cmd_probe_doc,
};

static int do_probe(int argc, char **argv)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    enum bridge_mode discovered;
    struct ahb *ahb;
    int rc;

    struct cmd_probe_args arguments = {0};
    rc = argp_parse(&cmd_probe_argp, argc, argv, ARGP_IN_ORDER, 0, &arguments);
    if (rc != 0) {
        rc = EXIT_FAILURE;
        goto done;
    }

    if ((rc = host_init(host, &arguments.connection) < 0)) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        rc = EXIT_FAILURE;
        goto done;
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = EXIT_FAILURE;
        goto cleanup_host;
    }

    if ((rc = soc_probe(soc, ahb)) < 0) {
        loge("Failed to probe SoC, exiting: %d\n", rc);
        goto cleanup_host;
    }

    if (arguments.list_ifaces) {
        soc_list_bridge_controllers(soc);
        rc = EXIT_SUCCESS;
    } else {
        if ((rc = soc_probe_bridge_controllers(soc, &discovered, arguments.connection.interface)) < 0) {
            loge("Failed to probe SoC bridge controllers: %d\n", rc);
            rc = EXIT_FAILURE;
            goto cleanup_soc;
        }

        rc = (arguments.requirement <= discovered) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

done:
    exit(rc);
}

static const struct cmd probe_cmd = {
    .name = "probe",
    .description = "Probe for any BMC",
    .fn = do_probe,
};
REGISTER_CMD(probe_cmd);
