// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

#include "ahb.h"
#include "ast.h"
#include "cmd.h"
#include "compiler.h"
#include "connection.h"
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

#define SFC_FLASH_WIN (64 << 10)

static char cmd_sfc_args_doc[] =
    "<erase|read|write> ADDRESS LENGTH [via DRIVER [INTERFACE [IP PORT USERNAME PASSWORD]]]";

static char cmd_sfc_doc[] =
    "\n"
    "SFC (SPI Flash Controller) command"
    "\v"
    "Supported sfcs':\n"
    "  fmc     Firmware memory controller\n"
    "\v"
    "Supported commands:\n"
    "  erase    Erase data on the specified address and length\n"
    "  read     Read data from the specified address and length to stdout\n"
    "  write    Write data on the specified address and length from stdin\n";

static struct argp_option cmd_sfc_options[] = {
    { "sfc", 's', "SFC", 0, "SFC to be used", 0 },
    { 0 },
};

enum flash_op { flash_op_read, flash_op_write, flash_op_erase };

struct cmd_sfc_args {
    struct connection_args connection;
    char *sfc;
    uint32_t address;
    uint32_t len;
    enum flash_op operation;
};

static error_t cmd_sfc_parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_sfc_args *arguments = state->input;
    int rc;

    switch (key) {
    case 's':
        if (strcmp("fmc", arg))
            argp_error(state, "Invalid SFC '%s'", arg);

        arguments->sfc = arg;
        break;
    case ARGP_KEY_ARG:
        if (!strcmp(arg, "via")) {
            rc = cmd_parse_via(state->next - 1, state, &arguments->connection);
            if (rc != 0)
                argp_error(state, "Failed to parse connection arguments. Returned code %d", rc);
            break;
        }

        switch (state->arg_num) {
        case 0:
            if (!strcmp("erase", arg))
                arguments->operation = flash_op_erase;
            else if (!strcmp("read", arg))
                arguments->operation = flash_op_read;
            else if (!strcmp("write", arg))
                arguments->operation = flash_op_write;
            else
                argp_error(state, "Invalid command '%s'", arg);
            break;
        case 1:
            arguments->address = strtoul(arg, NULL, 0);
            break;
        case 2:
            arguments->len = strtoul(arg, NULL, 0);
            break;
        }
       break;
   case ARGP_KEY_END:
       if (arguments->sfc == NULL)
           argp_error(state, "No SFC defined.");
       if (state->arg_num < 3)
           argp_error(state, "Not enough arguments provided.");
       break;
   default:
       return ARGP_ERR_UNKNOWN;
   }

    return 0;
}

static struct argp cmd_sfc_argp = {
    .options = cmd_sfc_options,
    .parser = cmd_sfc_parse_opt,
    .args_doc = cmd_sfc_args_doc,
    .doc = cmd_sfc_doc,
};

static int do_sfc(int argc, char **argv)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct flash_chip *chip;
    struct sfc *sfc;
    struct ahb *ahb;
    char *buf;
    int rc;

    struct cmd_sfc_args arguments = {0};
    rc = argp_parse(&cmd_sfc_argp, argc, argv, ARGP_IN_ORDER, 0, &arguments);
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

    rc = soc_probe(soc, ahb);
    if (rc < 0)
        goto cleanup_host;

    if (!(sfc = sfc_get_by_name(soc, arguments.sfc))) {
        loge("Failed to acquire SPI controller, exiting\n");
        goto cleanup_soc;
    }

    rc = flash_init(sfc, &chip);
    if (rc < 0)
        goto cleanup_soc;

    if (arguments.operation == flash_op_read) {
        ssize_t egress;

        buf = malloc(arguments.len);
        if (!buf)
            goto cleanup_flash;

        rc = flash_read(chip, arguments.address, buf, arguments.len);
        egress = write(1, buf, arguments.len);
        if (egress == -1) {
            rc = -errno;
            perror("write");
        }

        free(buf);
    } else if (arguments.operation == flash_op_write) {
        ssize_t ingress;

        arguments.len = SFC_FLASH_WIN;
        buf = malloc(arguments.len);
        if (!buf)
            goto cleanup_flash;

        while ((ingress = read(0, buf, arguments.len))) {
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
    } else if (arguments.operation == flash_op_erase) {
        rc = flash_erase(chip, arguments.address, arguments.len);
    }

cleanup_flash:
    flash_destroy(chip);

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    return rc;
}

static const struct cmd sfc_cmd = {
    .name = "sfc",
    .description = "Read, write or erase areas of a supported SFC",
    .fn = do_sfc,
};
REGISTER_CMD(sfc_cmd);
