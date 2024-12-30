// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024 Code Construct

#include "bits.h"
#include "cmd.h"
#include "compiler.h"
#include "host.h"
#include "log.h"
#include "rev.h"
#include "soc.h"
#include "soc/scu.h"
#include "soc/sdmc.h"

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

static int cmd_coprocessor_run(const char *name __unused, int argc, char *argv[])
{
    const char *arg_mem_base, *arg_mem_size;
    struct host _host, *host = &_host;
    unsigned long mem_base, mem_size;
    struct soc _soc, *soc = &_soc;
    struct soc_region dram;
    struct sdmc *sdmc;
    struct ahb *ahb;
    struct scu *scu;
    ssize_t src;
    char *endp;
    int rc;

    if (argc < 3) {
        loge("Not enough arguments for coprocessor command\n");
        return EXIT_FAILURE;
    }

    arg_mem_base = argv[1];
    arg_mem_size = argv[2];

    errno = 0;
    mem_base = strtoul(arg_mem_base, &endp, 0);
    if (mem_base == ULONG_MAX && errno) {
        loge("Failed to parse coprocessor RAM base '%s': %s\n", arg_mem_base, strerror(errno));
        return EXIT_FAILURE;
    } else if (arg_mem_base == endp || *endp) {
        loge("Failed to parse coprocessor RAM base '%s'\n", arg_mem_base);
        return EXIT_FAILURE;
    }

    errno = 0;
    mem_size = strtoul(arg_mem_size, &endp, 0);
    if (mem_size == ULONG_MAX && errno) {
        loge("Failed to parse coprocessor RAM size '%s': %s\n", arg_mem_size, strerror(errno));
        return EXIT_FAILURE;
    } else if (arg_mem_size == endp || *endp) {
        loge("Failed to parse coprocessor RAM size '%s'\n", arg_mem_size);
        return EXIT_FAILURE;
    }

    if (mem_size != COPROC_TOTAL_MEM_SIZE) {
        loge("We currently only support assigning 32M of memory to the coprocessor\n");
        return EXIT_FAILURE;
    }

    if ((rc = host_init(host, argc - 3, argv + 3)) < 0) {
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
    if (mem_base > UINT32_MAX) {
        loge("Provided RAM base 0x%ux exceeds SoC physical address space\n", mem_base);
        rc = EXIT_FAILURE;
        goto cleanup_soc;
    }
#endif

    if (((mem_base + mem_size) & UINT32_MAX) < mem_base) {
        loge("Invalid RAM region provided for coprocessor\n");
        rc = EXIT_FAILURE;
        goto cleanup_soc;
    }

    if (mem_base < dram.start || (mem_base + mem_size) > (dram.start + dram.length)) {
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
    if ((src = soc_siphon_in(soc, mem_base, mem_size, STDIN_FILENO)) < 0) {
        loge("Failed to load coprocessor firmware to provided region: %d\n", src);
        rc = EXIT_FAILURE;
        goto cleanup_scu;
    }

        /* 4. */
    if (scu_writel(scu, SCU_COPROC_MEM_BASE, mem_base) ||
        /* 5. */
        scu_writel(scu, SCU_COPROC_IMEM_LIMIT,
                   mem_base + COPROC_CACHED_MEM_SIZE) ||
        /* 6. */
        scu_writel(scu, SCU_COPROC_DMEM_LIMIT, mem_base + mem_size) ||
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

static int cmd_coprocessor_stop(const char *name __unused, int argc, char *argv[])
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct ahb *ahb;
    struct scu *scu;
    int rc;

    if ((rc = host_init(host, argc - 3, argv + 3)) < 0) {
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

static int do_coprocessor(const char *name __unused, int argc, char *argv[])
{
    const char *arg_subcmd;

    if (argc < 1) {
        loge("Not enough arguments for coprocessor command\n");
        return EXIT_FAILURE;
    }

    arg_subcmd = argv[0];

    if (!strcmp("run", arg_subcmd)) {
        return cmd_coprocessor_run(name, argc, argv);
    } else if (!strcmp("stop", arg_subcmd)) {
        return cmd_coprocessor_stop(name, argc, argv);
    } else {
        loge("Unknown coprocessor subcommand '%s'\n", arg_subcmd);
    }

    return EXIT_FAILURE;
}

static const struct cmd coprocessor_cmd = {
    "coprocessor",
    "<run ADDRESS LENGTH|stop> [INTERFACE [IP PORT USERNAME PASSWORD]]",
    do_coprocessor
};
REGISTER_CMD(coprocessor_cmd);
