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
#include "soc/clk.h"
#include "soc/sdmc.h"
#include "soc/sfc.h"
#include "soc/uart/vuart.h"
#include "soc/wdt.h"

#include <argp.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SFC_FLASH_WIN (64 << 10)

static char cmd_write_args_doc[] =
    "--type=<firmware|ram> [<ADDRESS> <LENGTH>] "
    "[via DRIVER [INTERFACE [IP PORT USERNAME PASSWORD]]]";

static char cmd_write_doc[] =
    "\n"
    "Write command"
    "\v"
    "NOTE: Only the 'ram' type can parse address and length and requires them!\n\n"
    "All data will be read from stdin.\n\n"
    "Supported types:\n"
    "  firmware    Write stdin to the FMC\n"
    "  ram         Write stdin to the memory\n";

enum cmd_write_mode {
    none,
    firmware,
    ram,
};

static struct argp_option cmd_write_options[] = {
    { "type", 't', "TYPE", 0, "Type to be write to", 0 },
    {0},
};

struct cmd_write_args {
    unsigned long mem_base;
    unsigned long mem_size;
    struct connection_args connection;
    enum cmd_write_mode mode;
};

static error_t cmd_write_parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_write_args *arguments = state->input;
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

        switch (state->arg_num) {
        case 0:
            parse_mem_arg(state, "write RAM base", &arguments->mem_base, arg);
            break;
        case 1:
            parse_mem_arg(state, "write RAM size", &arguments->mem_size, arg);
            break;
        }

        break;
    case ARGP_KEY_END:
        if (arguments->mode == none)
            argp_error(state, "No type to write to defined...");
        /* Other than the read command,this requires the arguments for ram */
        if (arguments->mode == ram && state->arg_num < 2)
            argp_error(state, "Not enough arguments for ram mode...");
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp cmd_write_argp = {
    .options = cmd_write_options,
    .parser = cmd_write_parse_opt,
    .args_doc = cmd_write_args_doc,
    .doc = cmd_write_doc,
};

static int cmd_write_firmware(struct cmd_write_args *arguments)
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

    if ((rc = host_init(host, &arguments->connection)) < 0) {
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

static int cmd_write_ram(struct cmd_write_args *arguments)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct soc_region dram;
    struct sdmc *sdmc;
    struct ahb *ahb;
    int rc;

    if ((rc = host_init(host, &arguments->connection)) < 0) {
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

    if (arguments->mem_base < dram.start ||
        (arguments->mem_base + arguments->mem_size) > (dram.start + dram.length)) {
        loge("Ill-formed RAM region provided for write\n");
        rc = -EINVAL;
        goto cleanup_soc;
    }

#if UINT32_MAX > SSIZE_MAX
    if (SSIZE_MAX < arguments->mem_size)
        return -EINVAL;
#endif

    if ((rc = soc_siphon_in(soc, arguments->mem_base, arguments->mem_size, STDIN_FILENO))) {
        loge("Failed to write to provided memory region: %d\n", rc);
    }

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    return rc;
}

static int do_write(int argc, char **argv)
{
    int rc;

    struct cmd_write_args arguments = {0};
    rc = argp_parse(&cmd_write_argp, argc, argv, ARGP_IN_ORDER, 0, &arguments);
    if (rc != 0)
        return rc;

    switch (arguments.mode) {
    case firmware:
        rc = cmd_write_firmware(&arguments);
        break;
    case ram:
        rc = cmd_write_ram(&arguments);
        break;
    /* If it reaches none here, then the argument parse logic is broken. */
    case none:
        loge("read: Reached 'none' mode after argument parsing. This is a bug!\n");
        rc = -EINVAL;
        break;
    }

    return rc;
}

static const struct cmd write_cmd = {
    .name = "write",
    .description = "Write data to the FMC or RAM",
    .fn = do_write,
};
REGISTER_CMD(write_cmd);
