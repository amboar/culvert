// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

#include "arg_helper.h"
#include "ahb.h"
#include "ast.h"
#include "compiler.h"
#include "host.h"
#include "log.h"
#include "priv.h"
#include "soc.h"
#include "soc/clk.h"
#include "soc/wdt.h"

#include <argp.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char doc[] =
    "\n"
    "Reset command\n"
    "\v"
    "Supported reset types:\n"
    "  soc     Reset the SoC\n\n"
    "Examples:\n\n"
    "  culvert reset -t soc -w wdt0\n"
    "  culvert reset -t soc -w wdt0 /dev/ttyUSB0\n";

static struct argp_option options[] = {
    {"type", 't', "TYPE", 0, "Reset type", 0},
    {"wdt", 'w', "WDT", 0, "Watchdog timer to use", 0},
    {0},
};

struct cmd_reset_args
{
    int key_arg_count;
    // change to enum once more types are supported
    int type;
    const char *wdt;
};

static error_t
parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_reset_args *arguments = state->input;

    if (key == ARGP_KEY_ARG)
        arguments->key_arg_count++;

    switch (key)
    {
    case 't':
        // Not in use right now, only a dumb check
        if (!strcmp("soc", arg))
            arguments->type = 1;
        else
            argp_error(state, "Invalid reset type '%s'", arg);
        break;
    case 'w':
        arguments->wdt = arg;
        break;
    case ARGP_KEY_ARG:
        break;
    case ARGP_KEY_END:
        if (!arguments->type)
            argp_error(state, "Missing reset type");
        if (!arguments->wdt)
            argp_error(state, "Missing watchdog timer");
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp argp = {
    options,
    parse_opt,
    "[INTERFACE]",
    doc,
    NULL,
    NULL,
    NULL,
};

int cmd_reset(struct argp_state *state)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct ahb *ahb;
    struct clk *clk;
    struct wdt *wdt;
    int cleanup;
    int rc;

    struct subcommand reset_cmd;
    struct cmd_reset_args arguments = {0};
    parse_subcommand(&argp, "reset", &arguments, state, &reset_cmd);

    // again, hack
    char **argv = reset_cmd.argv + 1 + (reset_cmd.argc - 1 - arguments.key_arg_count);

    if ((rc = host_init(host, arguments.key_arg_count, argv)) < 0) {
        loge("Failed to acquire AHB interface, exiting: %d\n", rc);
        exit(EXIT_FAILURE);
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        exit(EXIT_FAILURE);
    }

    /* Probe the SoC */
    if ((rc = soc_probe(soc, ahb)) < 0) {
        errno = -rc;
        perror("soc_probe");
        goto cleanup_host;
    }

    /* Initialise the required SoC drivers */
    if (!(clk = clk_get(soc))) {
        loge("Failed to acquire clock controller, exiting\n");
        goto cleanup_soc;
    }

    if (!(wdt = wdt_get_by_name(soc, arguments.wdt))) {
        loge("Failed to acquire %s controller, exiting\n", arguments.wdt);
        goto cleanup_soc;
    }

    /* Do the reset */
    if (!ahb->drv->local) {
        logi("Gating ARM clock\n");
        rc = clk_disable(clk, clk_arm);
        if (rc < 0)
            goto cleanup_soc;
    }

    logi("Preventing system reset\n");
    if ((rc = wdt_prevent_reset(soc)) < 0) {
        errno = -rc;
        perror("wdt_prevent_reset");
        goto clk_enable_arm;
    }

    /* wdt_perform_reset ungates the ARM if required */
    logi("Performing SoC reset\n");
    rc = wdt_perform_reset(wdt);
    if (rc < 0) {
clk_enable_arm:
        if ((cleanup = clk_enable(clk, clk_arm)) < 0) {
            errno = -cleanup;
            perror("clk_enable");
        }
    }

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    return rc;
}
