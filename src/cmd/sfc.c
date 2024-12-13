// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

#include "arg_helper.h"
#include "ahb.h"
#include "ast.h"
#include "compiler.h"
#include "flash.h"
#include "host.h"
#include "log.h"
#include "priv.h"
#include "soc/sfc.h"

#include <argp.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum flash_op { flash_op_read, flash_op_write, flash_op_erase };

static char doc[] =
    "\n"
    "SFC command\n"
    "\v"
    "Supported SFC types:\n"
    "  fmc     FMC controller\n"
    "\n"
    "Supported modes:\n"
    "  read    Read data from flash\n"
    "  write   Write data to flash\n"
    "  erase   Erase data from flash\n"
    "\n"
    "Examples:\n"
    "\n"
    "  culvert sfc -t fmc -m read -a 0x0 -l 0x1000\n";

static struct argp_option options[] = {
    {"type", 't', "TYPE", 0, "SFC type to access", 0},
    {"mode", 'm', "MODE", 0, "Operation to perform", 0},
    {"address", 'a', "ADDRESS", 0, "Address to access", 0},
    {"length", 'l', "LENGTH", 0, "Length of data to access", 0},
    {0},
};

struct cmd_sfc_args {
    uint32_t address;
    uint32_t length;
    int key_arg_count;
    const char *type;
    const char *mode;
    enum flash_op op;
};

static error_t
parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_sfc_args *arguments = state->input;

    if (key == ARGP_KEY_ARG)
        arguments->key_arg_count++;

    switch (key) {
    case 't':
        /* We only support fmc at this time. */
        if (strcmp("fmc", arg))
            argp_error(state, "Invalid SFC type '%s'", arg);

        arguments->type = arg;
        break;
    case 'm':
        if (!strcmp("read", arg))
            arguments->op = flash_op_read;
        else if (!strcmp("write", arg))
            arguments->op = flash_op_write;
        else if (!strcmp("erase", arg))
            arguments->op = flash_op_erase;
        else
            argp_error(state, "Invalid operation '%s'", arg);
        break;
    case 'a':
        arguments->address = strtoul(arg, NULL, 0);
        break;
    case 'l':
        arguments->length = strtoul(arg, NULL, 0);
        break;
    case ARGP_KEY_ARG:
        break;
    case ARGP_KEY_END:
        if (!arguments->type)
            argp_error(state, "Missing SFC type");
        if (arguments->op == 99)
            argp_error(state, "Missing operation");
        /* address could be 0 but the length can be defined */
        if (!arguments->address && !arguments->length)
            argp_error(state, "Missing address and length");
        /* if the length is still empty, throw error */
        if (!arguments->length)
            argp_error(state, "Missing length");
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp argp = {
    options,
    parse_opt,
    "[INTERFACE [IP PORT USERNAME PASSWORD]]",
    doc,
    NULL,
    NULL,
    NULL,
};

int cmd_sfc(struct argp_state *state)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct flash_chip *chip;
    struct sfc *sfc;
    struct ahb *ahb;
    char *buf;
    int rc;

    struct subcommand sfc_cmd;
    struct cmd_sfc_args arguments = {0};
    /* Set operation to 99, because read is 0 */
    arguments.op = 99;
    parse_subcommand(&argp, "sfc", &arguments, state, &sfc_cmd);

    char **argv_host = sfc_cmd.argv + 1 + (sfc_cmd.argc - 1 - arguments.key_arg_count);

    if ((rc = host_init(host, arguments.key_arg_count, argv_host)) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        exit(EXIT_FAILURE);
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = EXIT_FAILURE;
        goto cleanup_host;
    }

    rc = soc_probe(soc, ahb);
    if (rc < 0)
        goto cleanup_host;

    ;
    if (!(sfc = sfc_get_by_name(soc, "fmc"))) {
        loge("Failed to acquire SPI controller, exiting\n");
        goto cleanup_soc;
    }

    rc = flash_init(sfc, &chip);
    if (rc < 0)
        goto cleanup_soc;

    switch(arguments.op) {
    case flash_op_read:
        ssize_t egress;

        buf = malloc(arguments.length);
        if (!buf)
            goto cleanup_flash;

        rc = flash_read(chip, arguments.address, buf, arguments.length);
        egress = write(1, buf, arguments.length);
        if (egress == -1) {
            rc = -errno;
            perror("write");
        }

        free(buf);
        break;
    case flash_op_write:
        ssize_t ingress;

        arguments.length = SFC_FLASH_WIN;
        buf = malloc(arguments.length);
        if (!buf)
            goto cleanup_flash;

        while ((ingress = read(0, buf, arguments.length))) {
            if (ingress < 0) {
                rc = -errno;
                break;
            }

            rc = flash_write(chip, arguments.address, buf, ingress, true);
            if (rc < 0)
                break;

            arguments.address += ingress;
        }

        free(buf);
        break;
    case flash_op_erase:
        rc = flash_erase(chip, arguments.address, arguments.length);
        break;
    }

cleanup_flash:
    flash_destroy(chip);

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    return rc;
}
