// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
// Copyright (C) 2021, Oracle and/or its affiliates.

#include "arg_helper.h"
#include "ahb.h"
#include "ast.h"
#include "compiler.h"
#include "host.h"
#include "log.h"
#include "priv.h"
#include "soc.h"
#include "soc/trace.h"

#include <argp.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char doc[] =
    "\n"
    "Trace command\n"
    "\v"
    "Supported trace modes:\n"
    "  read        Trace read accesses\n"
    "  write       Trace write accesses\n\n"
    "Supported access widths:\n"
    "  1           8-bit access\n"
    "  2           16-bit access\n"
    "  4           32-bit access\n\n"
    "Example:\n\n"
    "  culvert trace -m read -a 0x1e788000 -w 1:0\n"
    "  culvert trace -m read -a 0x1e788000 -w 2:2\n"
    "  culvert trace -m write -a 0x1e788000 -w 4:0\n";

struct cmd_trace_args {
    uint32_t address;
    uint32_t width;
    enum trace_mode mode;
    int key_arg_count;
};

static struct argp_option options[] = {
    {"mode", 'm', "MODE", 0, "Trace mode (REQUIRED)", 0},
    {"address", 'a', "ADDRESS", 0, "Address to trace (REQUIRED)", 0},
    {"width", 'w', "WIDTH", 0, "Access width (REQUIRED)", 0},
    {0},
};

static error_t
parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_trace_args *arguments = state->input;

    if (key == ARGP_KEY_ARG)
        arguments->key_arg_count++;

    switch (key) {
        case 'm':
            if (!strcmp("read", arg))
                arguments->mode = trace_read;
            else if (!strcmp("write", arg))
                arguments->mode = trace_write;
            else
                argp_error(state, "Invalid trace mode '%s'", arg);
            break;
        case 'a':
            arguments->address = strtoul(arg, NULL, 0);
            break;
        case 'w':
            arguments->width = strtoul(arg, NULL, 0);
            if (arguments->width != 1 && arguments->width != 2 && arguments->width != 4)
                argp_error(state, "Invalid access width '%s'", arg);
            break;
        case ARGP_KEY_ARG:
            break;
        case ARGP_KEY_END:
            if (!arguments->address)
                argp_error(state, "Missing address");
            if (!arguments->width)
                argp_error(state, "Missing width");
            /* trace_read is 0, so this is a valid check */
            if (arguments->mode != trace_read && arguments->mode != trace_write)
                argp_error(state, "Missing mode");

            if (arguments->address & (arguments->width - 1))
                argp_error(state, "Address must be aligned to the access size");
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

int cmd_trace(struct argp_state *state)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct trace *trace;
    struct ahb *ahb;
    sigset_t set;
    int sig;
    int rc;

    struct subcommand trace_cmd;
    struct cmd_trace_args arguments = {0};
    parse_subcommand(&argp, "trace", &arguments, state, &trace_cmd);

    char **argv = trace_cmd.argv + 1 + (trace_cmd.argc - 1 - arguments.key_arg_count);

    if (sigemptyset(&set)) {
        rc = -errno;
        loge("Unable to initialise signal set: %d\n", rc);
        return rc;
    }

    if (sigaddset(&set, SIGINT)) {
        rc = -errno;
        loge("Unable to add SIGINT to signal set: %d\n", rc);
        return rc;
    }

    if ((rc = host_init(host, arguments.key_arg_count, argv)) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        exit(EXIT_FAILURE);
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = EXIT_FAILURE;
        goto cleanup_host;
    }

    if ((rc = soc_probe(soc, ahb)) < 0)
        goto cleanup_host;

    if (!(trace = trace_get(soc))) {
        loge("Unable to acquire trace controller\n");
        goto cleanup_soc;
    }

    if ((rc = trace_start(trace, arguments.address, arguments.width, arguments.mode))) {
        loge("Unable to start trace for 0x%08x %db %s: %d\n",
             arguments.address, arguments.width, arguments.mode, rc);
        goto cleanup_soc;
    }

    /*
     * The trace command waits for an unbounded amount of time while the trace
     * is collected; it's possible the bridge state may change while we're
     * waiting (for example if other culvert functions are being used).  We
     * attempt to handle this gracefully by releasing the bridge and then
     * reinitializing it later when we need to dump the trace.
     */
    if ((rc = host_bridge_release_from_ahb(ahb)) < 0) {
        loge("Failed to release AHB bridge: %d\n", rc);
        goto cleanup_soc;
    }

    sigprocmask(SIG_BLOCK, &set, NULL);
    if ((rc = sigwait(&set, &sig))) {
        rc = -rc;
        loge("Unable to wait for SIGINT: %d\n", rc);
        goto cleanup_soc;
    }
    sigprocmask(SIG_UNBLOCK, &set, NULL);

    if ((rc = host_bridge_reinit_from_ahb(ahb)) < 0) {
        loge("Failed to reinitialise AHB bridge: %d\n", rc);
        goto cleanup_soc;
    }

    if ((rc = trace_stop(trace))) {
        loge("Unable to stop trace: %d\n");
        goto cleanup_soc;
    }

    if ((rc = trace_dump(trace, 1)))
        loge("Unable to dump trace to stdout: %d\n", rc);

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    return rc;
}
