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

struct cmd_read_args {
    /* only used in the firmware/ram subcommand directly */
    int key_arg_count;
};

static struct argp_option options[] = {
    {0},
};

static char doc_firmware[] =
    "\n"
    "Read firmware command\n"
    "\v"
    "Read BMC firmware from flash and write to stdout\n";

static error_t
parse_opt_firmware(int key, char *arg __unused, struct argp_state *state)
{
    struct cmd_read_args *arguments = state->input;

    if (key == ARGP_KEY_ARG)
        arguments->key_arg_count++;

    switch (key) {
    case ARGP_KEY_ARG:
    case ARGP_KEY_END:
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp argp_firmware = {
    options,
    parse_opt_firmware,
    "[INTERFACE [IP PORT USERNAME PASSWORD]]",
    doc_firmware,
    NULL,
    NULL,
    NULL,
};

static int cmd_read_firmware(struct argp_state *state)
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

    struct subcommand firmware_cmd;
    struct cmd_read_args arguments = {0};
    parse_subcommand(&argp_firmware, "firmware", &arguments, state, &firmware_cmd);

    char **argv = firmware_cmd.argv + 1 + (firmware_cmd.argc - 1 - arguments.key_arg_count);

    if ((rc = host_init(host, arguments.key_arg_count, argv)) < 0) {
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

static char doc_ram[] =
    "\n"
    "Read RAM command\n"
    "\v"
    "Read RAM from the SoC and write to stdout\n\n"
    "If no arguments are provided, the entire DRAM region is dumped.\n"
    "If start and length are provided (via -S and -L), the specified region is dumped.\n";

struct cmd_read_ram_args {
    unsigned long start;
    unsigned long length;
    int key_arg_count;
};

static struct argp_option options_ram[] = {
    {"start", 'S', "ADDRESS", 0, "Start address of RAM region to dump", 0},
    {"length", 'L', "LENGTH", 0, "Length of RAM region to dump", 0},
    {0},
};

static error_t
parse_opt_ram(int key, char *arg, struct argp_state *state)
{
    struct cmd_read_ram_args *arguments = state->input;

    if (key == ARGP_KEY_ARG)
        arguments->key_arg_count++;

    switch (key) {
    case 'S':
        arguments->start = strtoul(arg, NULL, 0);
        if (arguments->start == ULONG_MAX && errno) {
            loge("Failed to parse RAM region start address '%s': %s\n", arg, strerror(errno));
            return ARGP_KEY_ERROR;
#if ULONG_MAX > UINT32_MAX
        } else if (arguments->start > UINT32_MAX) {
            loge("RAM region start address '%s' exceeds address space\n", arg);
            return ARGP_KEY_ERROR;
#endif // ULONG_MAX > UINT32_MAX
        }
        break;
    case 'L':
        arguments->length = strtoul(arg, NULL, 0);
        if (arguments->length == ULONG_MAX && errno) {
            loge("Failed to parse RAM region length '%s': %s\n", arg, strerror(errno));
            return ARGP_KEY_ERROR;
#if ULONG_MAX > UINT32_MAX
        } else if (arguments->length > UINT32_MAX) {
            loge("RAM region length '%s' exceeds address space\n", arg);
            return ARGP_KEY_ERROR;
#endif // ULONG_MAX > UINT32_MAX
        }
        break;
    case ARGP_KEY_ARG:
    case ARGP_KEY_END:
        if (UINT32_MAX - arguments->length < arguments->start)
            argp_error(state, "RAM region start address + length exceeds address space");
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp argp_ram = {
    options_ram,
    parse_opt_ram,
    "[INTERFACE [IP PORT USERNAME PASSWORD]]",
    doc_ram,
    NULL,
    NULL,
    NULL,
};

static int cmd_read_ram(struct argp_state *state)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct soc_region dram, vram;
    struct sdmc *sdmc;
    struct ahb *ahb;
    int rc;

    struct subcommand ram_cmd;
    struct cmd_read_ram_args arguments = {0};
    parse_subcommand(&argp_ram, "ram", &arguments, state, &ram_cmd);

    char **argv = ram_cmd.argv + 1 + (ram_cmd.argc - 1 - arguments.key_arg_count);

    if ((rc = host_init(host, arguments.key_arg_count, argv)) < 0) {
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

    if (arguments.start && arguments.length) {
        if (arguments.start < dram.start || (arguments.start + arguments.length) > (dram.start + dram.length)) {
            rc = -EINVAL;
            goto cleanup_soc;
        }

        logi("Dumping %" PRId32 "MiB (%#.8" PRIx32 "-%#.8" PRIx32 ")",
             arguments.length >> 20, arguments.start, arguments.start + arguments.length - 1);
    } else {
        if ((rc = sdmc_get_vram(sdmc, &vram)))
            goto cleanup_soc;

        arguments.start = dram.start;
        arguments.length = dram.length - vram.length;

        logi("%dMiB DRAM with %dMiB VRAM; dumping %dMiB (0x%x-0x%08x)\n",
             dram.length >> 20, vram.length >> 20,
             (dram.length - vram.length) >> 20, dram.start, vram.start - 1);
    }

    rc = soc_siphon_out(soc, arguments.start, arguments.length, STDOUT_FILENO);
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

static char doc_global[] =
    "\n"
    "Read command\n"
    "\v"
    "Supported read types:\n"
    "  firmware    Read BMC firmware\n"
    "  ram         Read RAM\n";

static error_t
parse_opt_global(int key, char *arg, struct argp_state *state)
{
    switch (key) {
    case ARGP_KEY_ARG:
        switch (state->arg_num) {
        case 0:
            if (!strcmp("firmware", arg))
                cmd_read_firmware(state);
            else if (!strcmp("ram", arg))
                cmd_read_ram(state);
            else
                argp_error(state, "Invalid read type '%s'", arg);
            break;
        default:
            break;
        }
        break;
    case ARGP_KEY_END:
        if (state->arg_num < 1)
            argp_usage(state);
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp argp_global = {
    options,
    parse_opt_global,
    "firmware|ram",
    doc_global,
    NULL,
    NULL,
    NULL,
};

int cmd_read(struct argp_state *state)
{
    struct subcommand read_cmd;
    struct cmd_read_args arguments = {0};
    parse_subcommand(&argp_global, "read", &arguments, state, &read_cmd);

    return 0;
}
