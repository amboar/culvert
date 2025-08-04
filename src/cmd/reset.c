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
#include "soc.h"
#include "soc/clk.h"
#include "soc/wdt.h"

#include <argp.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char cmd_reset_args_doc[] = "TARGET WDT [via DRIVER [INTERFACE [IP PORT USERNAME PASSWORD]]]";
static char cmd_reset_doc[] =
    "\n"
    "Reset command"
    "\v"
    "Supported targets:\n"
    "  soc              Reset the whole SOC\n"
    "\v"
    "Examples:\n"
    " Reset SoC hard via wdt3:\n"
    "  culvert reset soc wdt3\n\n"
    " Reset SoC hard via wdt2 (may causes FMC CS switch):\n"
    "  culvert reset soc wdt2\n";

static struct argp_option cmd_reset_options[] = {
    {0},
};

struct cmd_reset_args {
    struct connection_args connection;
    const char *wdt;
};

static error_t cmd_reset_parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_reset_args *arguments = state->input;
    int rc = 0;

    switch (key) {
    case ARGP_KEY_ARG:
        if (!strcmp(arg, "via")) {
            rc = cmd_parse_via(state->next - 1, state, &arguments->connection);
            if (rc != 0)
                argp_error(state, "Failed to parse connection arguments. Returned code %d", rc);
            return 0;
        }

        switch (state->arg_num) {
        case 0:
            /* Right now, we only support soc as reset target */
            if (strcmp("soc", arg))
                argp_error(state, "Invalid reset target '%s'", arg);
            break;
        case 1:
            arguments->wdt = arg;
            break;
        }
        break;
    case ARGP_KEY_END:
        if (state->arg_num < 2)
            argp_error(state, "Not enough arguments provided. Need type and watchdog.");
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp cmd_reset_argp = {
    .options = cmd_reset_options,
    .parser = cmd_reset_parse_opt,
    .args_doc = cmd_reset_args_doc,
    .doc = cmd_reset_doc,
};

static int do_reset(int argc, char **argv)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct ahb *ahb;
    struct clk *clk;
    struct wdt *wdt;
    int cleanup, rc;

    struct cmd_reset_args arguments = {0};
    rc = argp_parse(&cmd_reset_argp, argc, argv, ARGP_IN_ORDER, 0, &arguments);
    if (rc != 0)
        return rc;

    if ((rc = host_init(host, &arguments.connection)) < 0) {
        loge("Failed to acquire AHB interface, exiting: %d\n", rc);
        return rc;
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = EXIT_FAILURE;
        return rc;
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

static const struct cmd reset_cmd = {
    .name = "reset",
    .description = "Reset a component of the BMC chip",
    .fn = do_reset,
};
REGISTER_CMD(reset_cmd);
