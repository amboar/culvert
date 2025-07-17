// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024 Code Construct

#include "bits.h"
#include "cmd.h"
#include "compiler.h"
#include "connection.h"
#include "host.h"
#include "log.h"
#include "rev.h"
#include "soc.h"
#include "soc/scu.h"
#include "soc/sdmc.h"

#include <argp.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define COPROC_CACHED_MEM_SIZE (16 * 1024 * 1024)
#define COPROC_TOTAL_MEM_SIZE (32 * 1024 * 1024)

#define SCU_COPROC_CTRL 0xa00
#define   SCU_COPROC_CTRL_RESET_ASSERT BIT(1)
#define   SCU_COPROC_CTRL_EN BIT(0)

#define SCU_COPROC_MEM_BASE 0xa04
#define SCU_COPROC_IMEM_LIMIT 0xa08
#define SCU_COPROC_DMEM_LIMIT 0xa0c
#define SCU_COPROC_CACHE_RANGE 0xa40
#define   SCU_COPROC_CACHE_1ST_16MB_EN BIT(0)
#define SCU_COPROC_CACHE_FUNC 0xa48
#define   SCU_COPROC_CACHE_EN BIT(0)

static char cmd_coprocessor_args_doc[] = "<run ADDRESS LENGTH|stop> [via DRIVER [INTERFACE [IP PORT USERNAME PASSWORD]]]";
static char cmd_coprocessor_doc[] =
    "\n"
    "Coprocessor command"
    "\v"
    "Supported commands:\n"
    "  run         Run the coprocessor\n"
    "  stop        Stop the coprocessor\n";

enum cmd_coprocessor_mode {
    run,
    stop,
};

static struct argp_option cmd_coprocessor_options[] = {
    {0},
};

struct cmd_coprocessor_args {
    unsigned long mem_base;
    unsigned long mem_size;
    struct connection_args connection;
    enum cmd_coprocessor_mode mode;
    /* padding 4 bytes */
};

static error_t cmd_coprocessor_parse_opt(int key, char *arg,
                                         struct argp_state *state)
{
    struct cmd_coprocessor_args *arguments = state->input;
    int rc;

    switch (key) {
    case ARGP_KEY_ARG:
        if (!strcmp(arg, "via")) {
            rc = cmd_parse_via(state->next - 1, state, &arguments->connection);
            if (rc != 0)
                argp_error(state, "Failed to parse connection arguments. Returned code %d", rc);
            break;
        }

        if (state->arg_num == 0) {
            if (strcmp("run", arg) && strcmp("stop", arg))
                argp_error(state, "Invalid command '%s'", arg);

            if (!strcmp(arg, "run"))
                arguments->mode = run;
            else
                arguments->mode = stop;
            break;
        }

        /* If mode is not run, skip any further args */
        if (arguments->mode != run)
            break;

        if (state->arg_num == 1) {
            parse_mem_arg(state, "coprocessor RAM base", &arguments->mem_base, arg);
        } else if (state->arg_num == 2) {
            parse_mem_arg(state, "coprocessor RAM size", &arguments->mem_size, arg);
            if (arguments->mem_size != COPROC_TOTAL_MEM_SIZE)
                argp_error(state, "Only 32M memory is currently supported for the coprocessor");
        }

        break;
    case ARGP_KEY_END:
        if (arguments->mode == run && state->arg_num < 3)
            argp_error(state, "Not enough arguments for run mode...");
        if (arguments->mode == stop && state->arg_num < 1)
            argp_error(state, "Not enough arguments for stop mode...");
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp cmd_coprocessor_argp = {
    .options = cmd_coprocessor_options,
    .parser = cmd_coprocessor_parse_opt,
    .args_doc = cmd_coprocessor_args_doc,
    .doc = cmd_coprocessor_doc,
};

static int cmd_coprocessor_run(struct cmd_coprocessor_args *arguments)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct soc_region dram;
    struct sdmc *sdmc;
    struct ahb *ahb;
    struct scu *scu;
    ssize_t src;
    int rc;

    if ((rc = host_init(host, &arguments->connection)) < 0) {
        loge("Failed to initialise host interface: %d\n", rc);
        return EXIT_FAILURE;
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface\n");
        rc = EXIT_FAILURE;
        goto cleanup_host;
    }

    if ((rc = soc_probe(soc, ahb)) < 0) {
        loge("Failed to probe SoC: %d\n", rc);
        rc = EXIT_FAILURE;
        goto cleanup_host;
    }

    if (soc_generation(soc) != ast_g6) {
        loge("We currently only support the AST2600-series coprocessor\n");
        rc = EXIT_FAILURE;
        goto cleanup_soc;
    }

    if (!(sdmc = sdmc_get(soc))) {
        loge("Failed to acquire SDRAM memory controller\n");
        rc = EXIT_FAILURE;
        goto cleanup_soc;
    }

    if ((rc = sdmc_get_dram(sdmc, &dram))) {
        loge("Failed to locate DRAM: %d\n", rc);
        rc = EXIT_FAILURE;
        goto cleanup_soc;
    }

#if ULONG_MAX > UINT32_MAX
    if (arguments->mem_base > UINT32_MAX) {
        loge("Provided RAM base 0x%ux exceeds SoC physical address space\n", arguments->mem_base);
        rc = EXIT_FAILURE;
        goto cleanup_soc;
    }
#endif

    if (((arguments->mem_base + arguments->mem_size) & UINT32_MAX) < arguments->mem_base) {
        loge("Invalid RAM region provided for coprocessor\n");
        rc = EXIT_FAILURE;
        goto cleanup_soc;
    }

    if (arguments->mem_base < dram.start || (arguments->mem_base + arguments->mem_size) > (dram.start + dram.length)) {
        loge("Ill-formed RAM region provided for coprocessor\n");
        rc = EXIT_FAILURE;
        goto cleanup_soc;
    }

    if (!(scu = scu_get(soc))) {
        loge("Failed to acquire SCU driver\n");
        rc = EXIT_FAILURE;
        goto cleanup_soc;
    }

    /* 4.1.2 SSP Cache Programming Procedure
     *
     * 'AST2600 SECONDARY SERVICE PROCESSOR v0.1f.pdf'
     */
    /* 1. */
    if ((rc = scu_writel(scu, SCU_COPROC_CTRL, 0)) < 0) {
        loge("Failed to disable coprocoessor: %d\n", rc);
        rc = EXIT_FAILURE;
        goto cleanup_scu;
    }

    /* 2. */
    if ((rc = scu_writel(scu, SCU_COPROC_CTRL, SCU_COPROC_CTRL_RESET_ASSERT)) < 0) {
        loge("Failed to assert the coprocessor reset: %d", rc);
        rc = EXIT_FAILURE;
        goto cleanup_scu;
    }

    /* 3. */
    if ((src = soc_siphon_in(soc, arguments->mem_base, arguments->mem_size, STDIN_FILENO)) < 0) {
        loge("Failed to load coprocessor firmware to provided region: %d\n", src);
        rc = EXIT_FAILURE;
        goto cleanup_scu;
    }

        /* 4. */
    if (scu_writel(scu, SCU_COPROC_MEM_BASE, arguments->mem_base) ||
        /* 5. */
        scu_writel(scu, SCU_COPROC_IMEM_LIMIT,
                   arguments->mem_base + COPROC_CACHED_MEM_SIZE) ||
        /* 6. */
        scu_writel(scu, SCU_COPROC_DMEM_LIMIT, arguments->mem_base + arguments->mem_size) ||
        /* 7. */
        scu_writel(scu, SCU_COPROC_CACHE_RANGE, SCU_COPROC_CACHE_1ST_16MB_EN) ||
        /* 8. */
        scu_writel(scu, SCU_COPROC_CACHE_FUNC, SCU_COPROC_CACHE_EN)) {
        loge("Failed to configure coprocessor control registers\n");
        rc = EXIT_FAILURE;
        goto cleanup_scu;
    }

    if (usleep(1000) == -1) {
        loge("Coprocessor reset pre-delay failed: %s", strerror(errno));
        rc = EXIT_FAILURE;
        goto cleanup_scu;
    }

    /* 9. */
    if ((rc = scu_writel(scu, SCU_COPROC_CTRL, 0)) < 0) {
        loge("Failed to disable coprocessor: %d\n", rc);
        rc = EXIT_FAILURE;
        goto cleanup_scu;
    }

    if (usleep(1000) == -1) {
        loge("Coprocessor reset post-delay failed: %s", strerror(errno));
        rc = EXIT_FAILURE;
        goto cleanup_scu;
    }

    /* 10. */
    if ((rc = scu_writel(scu, SCU_COPROC_CTRL, SCU_COPROC_CTRL_EN)) < 0) {
        loge("Failed to start coprocessor: %d\n", rc);
        rc = EXIT_FAILURE;
        goto cleanup_scu;
    }

    rc = EXIT_SUCCESS;

cleanup_scu:
    scu_put(scu);

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    return rc;
}

static int cmd_coprocessor_stop(struct cmd_coprocessor_args *arguments)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct ahb *ahb;
    struct scu *scu;
    int rc;

    if ((rc = host_init(host, &arguments->connection)) < 0) {
        loge("Failed to initialise host interface: %d\n", rc);
        return EXIT_FAILURE;
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface\n");
        rc = EXIT_FAILURE;
        goto cleanup_host;
    }

    if ((rc = soc_probe(soc, ahb)) < 0) {
        loge("Failed to probe SoC: %d\n", rc);
        rc = EXIT_FAILURE;
        goto cleanup_host;
    }

    if (soc_generation(soc) != ast_g6) {
        loge("We currently only support the AST2600-series coprocessor\n");
        rc = EXIT_FAILURE;
        goto cleanup_soc;
    }

    if (!(scu = scu_get(soc))) {
        loge("Failed to acquire SCU driver\n");
        rc = EXIT_FAILURE;
        goto cleanup_soc;
    }

    if ((rc = scu_writel(scu, SCU_COPROC_CTRL, 0)) < 0) {
        loge("Failed to disable coprocoessor: %d\n", rc);
        rc = EXIT_FAILURE;
    }

    scu_put(scu);

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

    return rc;
}

static int do_coprocessor(int argc, char **argv)
{
    int rc;

    struct cmd_coprocessor_args arguments = {0};
    rc = argp_parse(&cmd_coprocessor_argp, argc, argv, ARGP_IN_ORDER, 0, &arguments);
    if (rc != 0) {
        rc = EXIT_FAILURE;
        goto done;
    }

    switch (arguments.mode) {
    case run:
        rc = cmd_coprocessor_run(&arguments);
        break;
    case stop:
        rc = cmd_coprocessor_stop(&arguments);
        break;
    }

done:
    return rc;
}

static const struct cmd coprocessor_cmd = {
    .name = "coprocessor",
    .description = "Do things on the coprocessors of the AST2600",
    .fn = do_coprocessor,
};
REGISTER_CMD(coprocessor_cmd);
