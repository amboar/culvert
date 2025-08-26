// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
// Copyright (C) 2021, Oracle and/or its affiliates.

#include "ahb.h"
#include "ast.h"
#include "cmd.h"
#include "compiler.h"
#include "connection.h"
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

static char cmd_trace_args_doc[] =
    "ADDRESS WIDTH MODE [via DRIVER [INTERFACE [IP PORT USERNAME PASSWORD]]]";

static char cmd_trace_doc[] =
    "\n"
    "Trace command"
    "\v"
    "Supported widths: 1,2,4\n\n"
    "Supported modes:\n"
    "  read        Trace read operations\n"
    "  write       Trace write operations\n";

static struct argp_option cmd_trace_options[] = {
    {0},
};

struct cmd_trace_args {
    unsigned long address;
    unsigned long width;
    enum trace_mode mode;
    struct connection_args connection;
};

static error_t cmd_trace_parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_trace_args *arguments = state->input;
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
            arguments->address = strtoul(arg, NULL, 0);
            break;
        case 1:
            arguments->width = strtoul(arg, NULL, 0);
            if (arguments->width != 1 && arguments->width != 2
                && arguments->width != 4)
                argp_error(state, "Invalid access size '%lu'", arguments->width);
            break;
        case 2:
            if (!strcmp(arg, "read"))
                arguments->mode = trace_read;
            else if (!strcmp(arg, "write"))
                arguments->mode = trace_write;
            else
                argp_error(state, "Invalid mode '%s'", arg);
            break;
        }
        break;
    case ARGP_KEY_END:
        if (arguments->address & (arguments->width - 1))
            argp_error(state, "Listening address must be aligned to the access size");
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp cmd_trace_argp = {
    .options = cmd_trace_options,
    .parser = cmd_trace_parse_opt,
    .args_doc = cmd_trace_args_doc,
    .doc = cmd_trace_doc,
};

//culvert trace ADDRESS WIDTH:OFFSET MODE
//culvert trace 0x1e788000 1:0 read
//culvert trace 0x1e788000 2:2 read
//culvert trace 0x1e788000 4:0 write
static int do_trace(int argc, char **argv)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct trace *trace;
    struct ahb *ahb;
    sigset_t set;
    int sig;
    int rc;

    struct cmd_trace_args arguments = {0};
    rc = argp_parse(&cmd_trace_argp, argc, argv, ARGP_IN_ORDER, 0, &arguments);
    if (rc != 0)
        return rc;

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

    if ((rc = host_init(host, &arguments.connection)) < 0) {
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

    if ((rc = trace_start(trace, arguments.address,
                          arguments.width, arguments.mode))) {
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

static const struct cmd trace_cmd = {
    .name = "trace",
    .description = "Trace what happens on a register",
    .fn = do_trace,
};
REGISTER_CMD(trace_cmd);
