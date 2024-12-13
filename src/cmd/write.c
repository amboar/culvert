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
#include "soc/clk.h"
#include "soc/sdmc.h"
#include "soc/sfc.h"
#include "soc/uart/vuart.h"
#include "soc/wdt.h"

#include <argp.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char doc_global[] =
    "\n"
    "Write command"
    "\v"
    "Supported subcommands:\n"
    "  firmware    Write firmware to flash\n"
    "  ram         Write data to RAM\n";

static char doc_firmware[] =
    "\n"
    "Write firmware command\n"
    "\v"
    "Write firmware image to flash\n"
    "\n"
    "Examples:\n\n"
    "  culvert write firmware < image.bin\n"
    "  culvert write firmware /dev/ttyUSB0 < image.bin\n";

struct cmd_write_firmware_args {
    int key_arg_count;
};

static struct argp_option options[] = {
    {0},
};

static error_t
parse_opt_firmware(int key, char *arg __unused, struct argp_state *state)
{
    struct cmd_write_firmware_args *arguments = state->input;

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

static int cmd_write_firmware(struct argp_state *state)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct ahb *ahb;
    struct flash_chip *chip;
    struct vuart *vuart;
    struct clk *clk;
    ssize_t ingress;
    struct sfc *sfc;
    int rc, cleanup;
    uint32_t phys;
    char *buf;

    struct subcommand firmware_cmd;
    struct cmd_write_firmware_args arguments = {0};
    parse_subcommand(&argp_firmware, "firmware", &arguments, state, &firmware_cmd);

    char **argv_host = firmware_cmd.argv + 1 + (firmware_cmd.argc - 1 - arguments.key_arg_count);

    if ((rc = host_init(host, arguments.key_arg_count, argv_host)) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        return rc;
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = -ENODEV;
        goto cleanup_host;
    }

    if ((rc = soc_probe(soc, ahb)) < 0) {
        loge("Failed to probe SoC: %d\n", rc);
        goto cleanup_host;
    }

    if (!(clk = clk_get(soc))) {
        loge("Failed to acquire clock controller, exiting\n");
        rc = -ENODEV;
        goto cleanup_soc;
    }

    if (!(vuart = vuart_get_by_name(soc, "vuart"))) {
        loge("Failed to acquire VUART controller, exiting\n");
        rc = -ENODEV;
        goto cleanup_soc;
    }

    if (ahb->drv->local)
        loge("I hope you know what you are doing\n");
    else {
        logi("Preventing system reset\n");
        if ((rc = wdt_prevent_reset(soc)) < 0)
            goto cleanup_state;

        logi("Gating ARM clock\n");
        if ((rc = clk_disable(clk, clk_arm)) < 0)
            goto cleanup_state;

        logi("Configuring VUART for host Tx discard\n");
        if ((rc = vuart_set_host_tx_discard(vuart, discard_enable)) < 0)
            goto cleanup_state;
    }

    logi("Initialising flash subsystem\n");
    if (!(sfc = sfc_get_by_name(soc, "fmc"))) {
        loge("Failed to acquire SPI flash controller, exiting\n");
        rc = -ENODEV;
        goto cleanup_state;
    }

    rc = flash_init(sfc, &chip);
    if (rc < 0)
        goto cleanup_state;

    /* FIXME: Make this common with the sfc write implementation */
    buf = malloc(SFC_FLASH_WIN);
    if (!buf) {
        rc = -ENOMEM;
        goto cleanup_flash;
    }

    logi("Writing firmware image\n");
    phys = 0;
    while ((ingress = read(0, buf, SFC_FLASH_WIN))) {
        if (ingress < 0) {
            rc = -errno;
            break;
        }

        do {
            if (ingress < SFC_FLASH_WIN) {
                loge("Unexpected ingress value: 0x%zx\n", ingress);
                goto cleanup_buf;
            }

            rc = flash_erase(chip, phys, ingress);
            if (rc < 0)
                goto cleanup_buf;

            rc = flash_write(chip, phys, buf, ingress, true);
        } while (rc == -EREMOTEIO); /* Miscompare */

        if (rc)
            break;

        phys += ingress;
    }

cleanup_buf:
    free(buf);

cleanup_flash:
    flash_destroy(chip);

cleanup_state:
    if (rc == 0) {
        if (!ahb->drv->local) {
            struct wdt *wdt;

            logi("Performing SoC reset\n");
            if (!(wdt = wdt_get_by_name(soc, "wdt2"))) {
                loge("Failed to acquire wdt2 controller, exiting\n");
                return -ENODEV;
            }

            rc = wdt_perform_reset(wdt);
            if (rc < 0) {
                return rc;
            }
        }
    } else {
        logi("Deconfiguring VUART host Tx discard\n");
        if ((cleanup = vuart_set_host_tx_discard(vuart, discard_disable))) {
            errno = -cleanup;
            perror("vuart_set_host_tx_discard");
        }

        logi("Ungating ARM clock\n");
        if ((cleanup = clk_enable(clk, clk_arm))) {
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

static char doc_ram[] =
    "\n"
    "Write RAM command"
    "\v"
    "Write data to RAM\n"
    "\n"
    "Examples:\n\n"
    "  culvert write ram -a 0x10000000 -l 1000 < test.bin\n"
    "  culvert write ram -a 0x10000000 -l 1000 /dev/ttyUSB0 < test.bin\n";

static struct argp_option options_ram[] = {
    {"address", 'a', "ADDRESS", 0, "Address to write to (required)", 0},
    {"length", 'l', "LENGTH", 0, "Length of data to write (required)", 0},
    {0},
};

struct cmd_write_ram_args {
    unsigned long start;
    unsigned long length;
    int key_arg_count;
};

static error_t
parse_opt_ram(int key, char *arg, struct argp_state *state)
{
    struct cmd_write_ram_args *arguments = state->input;
    unsigned long val;

    if (key == ARGP_KEY_ARG)
        arguments->key_arg_count++;

    switch (key) {
    case 'a':
        errno = 0;
        val = strtoul(arg, NULL, 0);
        if (val == ULONG_MAX && errno)
            argp_error(state, "Failed to parse address '%s': %s", arg, strerror(errno));
#if ULONG_MAX > UINT32_MAX
        else if (val > UINT32_MAX)
            argp_error(state, "Address '%s' exceeds address space", arg);
#endif // ULONG_MAX > UINT32_MAX
        arguments->start = val;
        break;
    case 'l':
        errno = 0;
        val = strtoul(arg, NULL, 0);
        if (val == ULONG_MAX && errno)
            argp_error(state, "Failed to parse length '%s': %s", arg, strerror(errno));
#if ULONG_MAX > UINT32_MAX
        else if (val > UINT32_MAX)
            argp_error(state, "Length '%s' exceeds address space", arg);
#endif // ULONG_MAX > UINT32_MAX
        arguments->length = val;
        break;
    case ARGP_KEY_ARG:
    case ARGP_KEY_END:
        if (!arguments->start || !arguments->length)
            argp_error(state, "Missing start or length");
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

static int cmd_write_ram(struct argp_state *state)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct soc_region dram;
    struct sdmc *sdmc;
    struct ahb *ahb;
    int rc;

    struct subcommand ram_cmd;
    struct cmd_write_ram_args arguments = {0};
    parse_subcommand(&argp_ram, "ram", &arguments, state, &ram_cmd);

    char **argv_host = ram_cmd.argv + 1 + (ram_cmd.argc - 1 - arguments.key_arg_count);

    if ((rc = host_init(host, arguments.key_arg_count, argv_host)) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        return rc;
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = EXIT_FAILURE;
        goto cleanup_host;
    }

    if ((rc = soc_probe(soc, ahb)) < 0) {
        loge("Failed to probe SoC: %d\n", rc);
        goto cleanup_host;
    }

    if (!(sdmc = sdmc_get(soc))) {
        loge("Failed to acquire SDRAM memory controller\n");
        rc = -ENODEV;
        goto cleanup_soc;
    }

    if ((rc = sdmc_get_dram(sdmc, &dram))) {
        loge("Failed to locate DRAM: %d\n", rc);
        goto cleanup_soc;
    }

    if (((arguments.start + arguments.length) & UINT32_MAX) < arguments.start) {
        loge("Invalid RAM region provided for write\n");
        rc = -EINVAL;
        goto cleanup_soc;
    }

    if (arguments.start < dram.start ||
        (arguments.start + arguments.length) > (dram.start + dram.length)) {
        loge("Ill-formed RAM region provided for write\n");
        rc = -EINVAL;
        goto cleanup_soc;
    }

#if UINT32_MAX > SSIZE_MAX
    if (SSIZE_MAX < arguments.length) {
        return -EINVAL;
    }
#endif

    if ((rc = soc_siphon_in(soc, arguments.start, arguments.length, STDIN_FILENO))) {
        loge("Failed to write to provided memory region: %d\n", rc);
    }

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    return rc;
}

static error_t
parse_opt_global(int key, char *arg, struct argp_state *state)
{
    switch (key) {
    case ARGP_KEY_ARG:
        if (state->arg_num == 0) {
            if (!strcmp("firmware", arg))
                cmd_write_firmware(state);
            else if (!strcmp("ram", arg))
                cmd_write_ram(state);
            else
                argp_error(state, "Invalid write type '%s'", arg);
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

int cmd_write(struct argp_state *state)
{
    struct subcommand global_cmd;
    struct cmd_write_firmware_args arguments = {0};
    parse_subcommand(&argp_global, "write", &arguments, state, &global_cmd);

    return 0;
}
