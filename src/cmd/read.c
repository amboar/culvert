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
#include "soc.h"
#include "soc/sdmc.h"
#include "soc/sfc.h"

#include <argp.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char cmd_read_args_doc[] =
    "--type=<firmware|ram> [<ADDRESS> <LENGTH>] "
    "[via DRIVER [INTERFACE [IP PORT USERNAME PASSWORD]]]";

static char cmd_read_doc[] =
    "\n"
    "Read command"
    "\v"
    "NOTE: Only the 'ram' type can parse address and length!\n\n"
    "All data will be written to stdout.\n\n"
    "Supported types:\n"
    "  firmware    Read the content from the FMC\n"
    "  ram         Read the content from the memory\n";

enum cmd_read_mode {
    none,
    firmware,
    ram,
};

static struct argp_option cmd_read_options[] = {
    { "type", 't', "TYPE", 0, "Type to be read from", 0 },
    {0},
};

struct cmd_read_args {
    unsigned long mem_base;
    unsigned long mem_size;
    struct connection_args connection;
    enum cmd_read_mode mode;
};

static error_t cmd_read_parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_read_args *arguments = state->input;
    int rc;

    switch (key) {
    case 't':
        if (strcmp(arg, "firmware") && strcmp(arg, "ram"))
            argp_error(state, "Invalid type '%s'", arg);

        if (!strcmp(arg, "firmware"))
            arguments->mode = firmware;
        else
            arguments->mode = ram;
        break;
    case ARGP_KEY_ARG:
        if (!strcmp(arg, "via")) {
            rc = cmd_parse_via(state->next - 1, state, &arguments->connection);
            if (rc != 0)
                argp_error(state, "Failed to parse connection arguments. Returned code %d", rc);
            break;
        }

        /* If mode is not ram, skip any further args */
        if (arguments->mode != ram)
            break;

        if (state->arg_num == 0)
            parse_mem_arg(state, "read RAM base", &arguments->mem_base, arg);
        else if (state->arg_num == 1)
            parse_mem_arg(state, "read RAM size", &arguments->mem_size, arg);

        break;
    case ARGP_KEY_END:
        if (arguments->mode == none)
            argp_error(state, "No type to be read from defined...");
        /* It is possible to not pass the ram base and size */
        if (arguments->mode == ram && (state->arg_num > 0 && state->arg_num < 2))
            argp_error(state, "Not enough arguments for ram mode...");
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp cmd_read_argp = {
    .options = cmd_read_options,
    .parser = cmd_read_parse_opt,
    .args_doc = cmd_read_args_doc,
    .doc = cmd_read_doc,
};

static int cmd_read_firmware(struct cmd_read_args *arguments)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct ahb *ahb;
    struct soc_region flash;
    struct flash_chip *chip;
    struct sfc *sfc;
    uint32_t wp;
    int cleanup;
    int rc;

    if ((rc = host_init(host, &arguments->connection)) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        return rc;
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = -ENODEV;
        goto cleanup_host;
    }

    if ((rc = soc_probe(soc, ahb)) < 0)
        goto cleanup_host;

    logi("Initialising flash controller\n");
    if (!(sfc = sfc_get_by_name(soc, "fmc"))) {
        loge("Failed to acquire SPI controller\n");
        rc = -ENODEV;
        goto cleanup_soc;
    }

    logi("Initialising flash chip\n");
    if ((rc = flash_init(sfc, &chip))) {
        goto cleanup_soc;
    }

    logi("Write-protecting all chip-selects\n");
    if ((rc = sfc_write_protect_save(sfc, true, &wp)))
        goto cleanup_chip;

    if ((rc = sfc_get_flash(sfc, &flash)) < 0)
        goto cleanup_chip;

    logi("Exfiltrating BMC flash to stdout\n\n");
    rc = soc_siphon_out(soc, flash.start, chip->info.size, 1);
    if (rc) { errno = -rc; perror("soc_siphon_in"); }

    if ((cleanup = sfc_write_protect_restore(sfc, wp)) < 0) {
        errno = -rc;
        perror("sfc_write_protect_restore");
    }

cleanup_chip:
    flash_destroy(chip);

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    return rc;
}

static int cmd_read_ram(struct cmd_read_args *arguments)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct soc_region dram, vram;
    struct sdmc *sdmc;
    struct ahb *ahb;
    int rc;

    if (UINT32_MAX - arguments->mem_size < arguments->mem_base)
        return -EINVAL;

    if ((rc = host_init(host, &arguments->connection)) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        return rc;
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = -ENODEV;
        goto cleanup_host;
    }

    if ((rc = soc_probe(soc, ahb)) < 0)
        goto cleanup_host;


    if (!(sdmc = sdmc_get(soc))) {
        rc = -ENODEV;
        goto cleanup_soc;
    }

    if ((rc = sdmc_get_dram(sdmc, &dram))) {
        goto cleanup_soc;
    }

    if (arguments->mem_base && arguments->mem_size) {
        if (arguments->mem_base < dram.start ||
            (arguments->mem_base + arguments->mem_size) > (dram.start + dram.length)) {
            loge("Ill-formed RAM region provided for write\n");
            rc = -EINVAL;
            goto cleanup_soc;
        }

        logi("Dumping %" PRId32 "MiB (%#.8" PRIx32 "-%#.8" PRIx32 ")",
             arguments->mem_size >> 20, arguments->mem_base,
             arguments->mem_base + arguments->mem_size - 1);
    } else {
        if ((rc = sdmc_get_vram(sdmc, &vram))) {
            goto cleanup_soc;
        }

        arguments->mem_base = dram.start;
        arguments->mem_size = dram.length - vram.length;

        logi("%dMiB DRAM with %dMiB VRAM; dumping %dMiB (0x%x-0x%08x)\n",
             dram.length >> 20, vram.length >> 20,
             (dram.length - vram.length) >> 20, dram.start, vram.start - 1);
    }

    rc = soc_siphon_out(soc, arguments->mem_base, arguments->mem_size, STDOUT_FILENO);
    if (rc) {
        errno = -rc;
        perror("soc_siphon_in");
    }

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    return rc;
}

static int do_read(int argc, char **argv)
{
    int rc;

    struct cmd_read_args arguments = {0};
    arguments.mode = none;
    rc = argp_parse(&cmd_read_argp, argc, argv, ARGP_IN_ORDER, 0, &arguments);
    if (rc != 0)
        return rc;

    switch (arguments.mode) {
    case firmware:
        rc = cmd_read_firmware(&arguments);
        break;
    case ram:
        rc = cmd_read_ram(&arguments);
        break;
    /* If it reaches none here, then the argument parse logic is broken. */
    case none:
        loge("read: Reached 'none' mode after argument parsing. This is a bug!\n");
        rc = -EINVAL;
        break;
    }

    return rc;
}

static const struct cmd read_cmd = {
    .name = "read",
    .description = "Read data from the FMC or RAM",
    .fn = do_read,
};
REGISTER_CMD(read_cmd);
