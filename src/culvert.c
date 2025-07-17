// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
// Copyright (C) 2021, Oracle and/or its affiliates.

/* For program_invocation_short_name */
#define _GNU_SOURCE

#include <argp.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd.h"
#include "config.h"
#include "compiler.h"
#include "log.h"
#include "version.h"
#include "ahb.h"
#include "host.h"

#include "ccan/autodata/autodata.h"

const char *argp_program_version = "culvert " CULVERT_VERSION;
const char *argp_program_bug_address = "GitHub amboar/culvert";

struct root_arguments {
    char *name;
    char **args;
    int argc;
    struct cmd *cmd;
};

static struct argp_option root_options[] = {
    { "verbose", 'v', 0, 0, "Get verbose output", 0 },
    { "quiet", 'q', 0, 0, "Don't produce any output", 0 },
    { "skip-bridge", 's', "BRIDGE", 0, "Skip BRIDGE driver", 0 },
    { "list-bridges", 'l', 0, 0, "List available bridge drivers", 0 },
    {0}
};

static char *make_root_doc(struct cmd **cmds, size_t n_cmds)
{
    char *doc = NULL;
    if (asprintf(&doc, "\nCulvert — A Test and Debug Tool for BMC AHB Interfaces\n"
                       "\vAvailable commands:\n") < 0)
        return NULL;

    for (size_t i = 0; i < n_cmds; i++) {
        char *new_doc;
        if (asprintf(&new_doc, "%s  %-20s %s\n", doc, cmds[i]->name,
                     cmds[i]->description ? cmds[i]->description : "") < 0) {
            free(doc);
            return NULL;
        }
        free(doc);
        doc = new_doc;
    }

    return doc;
}

static error_t
parse_root_opt(int key, char *arg, struct argp_state *state)
{
    struct root_arguments *arguments = state->input;

    switch (key) {
        case 'q':
            log_set_level(level_none);
            break;
        case 'v':
            log_set_level(level_trace);
            break;
        case 's':
            if (disable_bridge_driver(arg)) {
                fprintf(stderr, "Error: '%s' not a recognized bridge name (use '-l' to list)\n", arg);
                return -EINVAL;
            }
            break;
        case 'l':
            print_bridge_drivers();
            /* Early exit as we only print the bridge drivers */
            exit(EXIT_SUCCESS);
            break;
        case ARGP_KEY_ARG:
            /* Ensure that only the first argument is being used as name */
            if (!arguments->name)
                arguments->name = arg;
            else
                argp_usage(state);

            /* Skip other arguments but save them to args array */
            arguments->args = &state->argv[state->next];
            arguments->argc = state->argc - state->next;
            state->next = state->argc;
            break;
        case ARGP_KEY_END:
            if (!arguments->name)
                argp_usage(state);
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

int main(int argc, char **argv)
{
    struct cmd **cmds;
    struct root_arguments args = {0};
    size_t n_cmds = 0;
    int rc = 0;

    /*
     * Always initialise the log level and set a different level if an
     * argument is passed.
     */
    log_set_level(level_info);

    /* Get all commands via autodata */
    cmds = autodata_get(cmds, &n_cmds);
    qsort(cmds, n_cmds, sizeof(void *), cmd_cmp);

    char *doc = make_root_doc(cmds, n_cmds);

    /* argp struct must be in main because of doc generation. */
    struct argp root_argp = {
        .options = root_options,
        .parser = parse_root_opt,
        .args_doc = "<cmd> [CMD_OPTIONS]...",
        .doc = doc,
    };

    /*
     * Parse command-line arguments
     * NOTE: `ARGP_IN_ORDER` is required to make sub-commands work
     * properly and to enforce options to be in order.
     */
    rc = argp_parse(&root_argp, argc, argv, ARGP_IN_ORDER, 0, &args);
    free(doc);
    if (rc != 0)
        goto cleanup_cmds;

    /* If for some reason args.name is still empty but rc is 0, break here */
    if (args.name == 0)
        goto cleanup_cmds;

    struct cmd *selected = NULL;
    for (size_t i = 0; i < n_cmds; i++) {
        if (!strcmp(args.name, cmds[i]->name)) {
            selected = cmds[i];
            break;
        }
    }

    if (!selected) {
        fprintf(stderr, "Unknown command: %s\n", args.name);
        rc = -EINVAL;
        goto cleanup_cmds;
    }

    /* Allocate new array for the command */
    char **subcmd_argv = calloc(args.argc + 1, sizeof(char *));
    if (!subcmd_argv) {
        rc = -ENOMEM;
        goto cleanup_cmds;
    }

    /* Set argv[0] to a combined name like "culvert coprocessor" */
    if (asprintf(&subcmd_argv[0], "%s %s", argv[0], args.name) < 0) {
        rc = -ENOMEM;
        goto cleanup_cmds;
    }

    /* Copy the remaining arguments from args.args */
    for (int i = 0; i < args.argc; i++)
        subcmd_argv[i + 1] = args.args[i];

    rc = selected->fn(args.argc + 1, subcmd_argv);
    free(subcmd_argv[0]);
    free(subcmd_argv);

cleanup_cmds:
    autodata_free(cmds);
    exit(rc ? EXIT_FAILURE : EXIT_SUCCESS);
}
