// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

#include "arg_helper.h"
#include "compiler.h"
#include "host.h"
#include "log.h"
#include "soc.h"

#include <argp.h>
#include <stdio.h>

static char doc[] =
    "\n"
    "Probe command"
    "\v"
    "Supported requirements:\n"
    "  integrity        Require integrity\n"
    "  confidentiality  Require confidentiality\n";

static struct argp_option options[] = {
    {"list-interfaces", 'l', 0, 0, "List available interfaces", 0},
    {"interface", 'i', "INTERFACE", 0, "Interface to probe", 0},
    {"require", 'r', "REQUIREMENT", 0, "Requirement to probe for", 0},
    {0},
};

struct cmd_probe_args
{
    const char *interface;
    const char *ip;
    int port;
    const char *username;
    const char *password;
    const char *iface;
    enum bridge_mode requirement;
    int key_arg_count;
    int list_ifaces;
};

static error_t
parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_probe_args *arguments = state->input;

    if (key == ARGP_KEY_ARG)
        arguments->key_arg_count++;

    switch (key)
    {
    case 'l':
        arguments->list_ifaces = 1;
        break;
    case 'i':
        arguments->iface = arg;
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
        switch (state->arg_num)
        {
        case 0:
            arguments->interface = arg;
            break;
        case 1:
            arguments->ip = arg;
            break;
        case 2:
            arguments->port = atoi(arg);
            break;
        case 3:
            arguments->username = arg;
            break;
        case 4:
            arguments->password = arg;
            break;
        default:
            break;
        }
        break;
    case ARGP_KEY_END:
        if (!arguments->requirement)
            arguments->requirement = bm_permissive;

        if (!arguments->iface)
            arguments->iface = NULL;

        if (arguments->key_arg_count > 1 && arguments->key_arg_count != 5)
            argp_error(state, "Wrong number of arguments. Either 1 or 5");
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp argp = {
    options,
    parse_opt,
    "[-l] [-i INTERFACE] [-r REQUIREMENT]",
    doc,
    NULL,
    NULL,
    NULL,
};

int cmd_probe(struct argp_state *state)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    enum bridge_mode discovered;
    struct ahb *ahb;
    int rc;

    struct subcommand probe_cmd;
    struct cmd_probe_args arguments = {0};
    parse_subcommand(&argp, "probe", &arguments, state, &probe_cmd);

    char **argv = probe_cmd.argv + 1 + (probe_cmd.argc - 1 - arguments.key_arg_count);

    if ((rc = host_init(host, arguments.key_arg_count, argv) < 0))
    {
        loge("Failed to initialise host interfaces: %d\n", rc);
        rc = EXIT_FAILURE;
        goto done;
    }

    if (!(ahb = host_get_ahb(host)))
    {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = EXIT_FAILURE;
        goto cleanup_host;
    }

    if ((rc = soc_probe(soc, ahb)) < 0)
    {
        loge("Failed to probe SoC, exiting: %d\n", rc);
        goto cleanup_host;
    }

    if (arguments.list_ifaces)
    {
        soc_list_bridge_controllers(soc);
        rc = EXIT_SUCCESS;
    }
    else
    {
        if ((rc = soc_probe_bridge_controllers(soc, &discovered, arguments.iface)) < 0)
        {
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
