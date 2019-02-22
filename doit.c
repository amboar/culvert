// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/types.h>
#include <unistd.h>

#include "array.h"
#include "ahb.h"
#include "ast.h"
#include "debug.h"
#include "ilpc.h"
#include "l2a.h"
#include "log.h"
#include "p2a.h"
#include "mb.h"
#include "priv.h"

/* Buffer sizes */
#define BMC_FLASH_LEN   (32 << 20)
#define DUMP_RAM_WIN  (8 << 20)
#define SFC_FLASH_WIN (1 << 20)

static void help(const char *name)
{
    printf("%s: " VERSION "\n", name);
    printf("Usage:\n");
    printf("\n");
    printf("%s probe\n", name);
    printf("%s devmem read ADDRESS\n", name);
    printf("%s devmem write ADDRESS VALUE\n", name);
}

static int access_ahb(const char *name, int argc, char *argv[],
                      struct ahb *ahb)
{
    uint32_t address, data;
    bool action_read;
    int rc;

    if (argc < 2) {
        loge("Not enough arguments for ilpc command\n");
        help(name);
        exit(EXIT_FAILURE);
    }

    if (!strcmp("read", argv[0]))
        action_read = true;
    else if (!strcmp("write", argv[0]))
        action_read = false;
    else {
        loge("Unknown action: %s\n", argv[0]);
        help(name);
        exit(EXIT_FAILURE);
    }

    address = strtoul(argv[1], NULL, 0);

    if (!action_read) {
        if (argc < 3) {
            loge("Not enough arguments for ilpc write command\n");
            help(name);
            exit(EXIT_FAILURE);
        }
        data = strtoul(argv[2], NULL, 0);
    }

    if (action_read) {
        rc = ahb_readl(ahb, address, &data);
        if (rc) {
            errno = -rc;
            perror("ahb_readl");
            exit(EXIT_FAILURE);
        }
        printf("0x%08x: 0x%08x\n", address, le32toh(data));
    } else {
        rc = ahb_writel(ahb, address, htole32(data));
        if (rc) {
            errno = -rc;
            perror("ahb_writel");
            exit(EXIT_FAILURE);
        }
    }

    return 0;
}

static int cmd_devmem(const char *name, int argc, char *argv[])
{
    struct devmem _devmem, *devmem = &_devmem;
    struct ahb _ahb, *ahb = &_ahb;
    int cleanup;
    int rc;

    rc = devmem_init(devmem);
    if (rc < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(name);
        } else {
            errno = -rc;
            perror("devmem_init");
        }
        exit(EXIT_FAILURE);
    }

    ahb_use(ahb, ahb_devmem, devmem);
    rc = access_ahb(name, argc, argv, ahb);
    if (rc) {
        errno = -rc;
        perror("access_ahb");
        exit(EXIT_FAILURE);
    }

    cleanup = devmem_destroy(devmem);
    if (cleanup) {
        errno = -cleanup;
        perror("devmem_destroy");
        exit(EXIT_FAILURE);
    }

    return 0;
}

static void cmd_probe_help(const char *name, int argc, char *argv[])
{
    static const char *probe_help =
        "Usage:\n"
        "%s probe --help\n"
        "%s probe --interface INTERFACE ...\n"
        "%s probe --list-interfaces\n"
        "%s probe --require <integrity|confidentiality>\n";

    printf(probe_help, name, name, name, name);
}

const char *ahb_interfaces[] = { "ilpc", "p2a", "xdma", "debug" };

static void cmd_probe_list_interfaces(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(ahb_interfaces); i++)
        printf("%s\n", ahb_interfaces[i]);
}

static bool cmd_probe_validate_interface(const char *iface)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(ahb_interfaces); i++)
        if (!strcmp(ahb_interfaces[i], iface))
            return true;

    return false;
}

static const enum log_colour cmd_probe_pr_ip_colours[] = {
    [ip_state_unknown] = colour_yellow,
    [ip_state_absent] = colour_white,
    [ip_state_enabled] = colour_red,
    [ip_state_disabled] = colour_green,
};

static void cmd_probe_pr_ip(enum ast_ip_state state, const char *fmt, ...)
{
    int fd = fileno(stdout);
    va_list args;
    int rc;

    va_start(args, fmt);
    rc = vdprintf(fd, fmt, args);
    va_end(args);
    if (rc < 0) {
        perror("vprintf");
        return;
    }

    log_highlight(fd, cmd_probe_pr_ip_colours[state], ast_ip_state_desc[state]);

    rc = write(fd, "\n", 1);
    if (rc < 0) {
        perror("write");
        return;
    }
}

static void cmd_probe_pr(bool result, const char *t, const char *f,
                         const char *fmt, ...)
{
    int fd = fileno(stdout);
    va_list args;
    int rc;

    va_start(args, fmt);
    rc = vdprintf(fd, fmt, args);
    va_end(args);
    if (rc < 0) {
        perror("vprintf");
        return;
    }

    if (result) {
        log_highlight(fd, colour_red, t);
    } else {
        log_highlight(fd, colour_green, f);
    }

    rc = write(fd, "\n", 1);
    if (rc < 0) {
        perror("write");
        return;
    }
}

static int cmd_probe_range_cmp(const void *a, const void *b)
{
    const int64_t as = (*(struct ahb_range **)a)->start;
    const int64_t bs = (*(struct ahb_range **)b)->start;

    return as - bs;
}

static void cmd_probe_sort_ranges(struct ahb_range *src,
                                  struct ahb_range **dst, size_t nmemb)
{
    int i;

    for (i = 0; i < p2ab_ranges_max; i++) {
        dst[i] = &src[i];
    }
    qsort(dst, nmemb, sizeof(src), cmd_probe_range_cmp);
}

static int cmd_probe(const char *name, int argc, char *argv[])
{
    struct ast_interfaces ifaces;
    bool pass_requirement = true;
    char *opt_interface = NULL;
    char *opt_require = NULL;
    int rc;
    int c;
    int i;

    while (1) {
        int option_index = 0;

        static struct option long_options[] = {
            { "help", no_argument, NULL, 'h' },
            { "interface", required_argument, NULL, 'i' },
            { "list-interfaces", no_argument, NULL, 'l' },
            { "require", required_argument, NULL, 'r' },
            { },
        };

        c = getopt_long(argc, argv, "hi:lr:", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'h':
                cmd_probe_help(name, argc, argv);
                rc = EXIT_SUCCESS;
                goto done;
            case 'i':
                if (!cmd_probe_validate_interface(optarg)) {
                    loge("Unrecognised interface: %s\n", optarg);
                    loge("Valid interfaces:\n");
                    cmd_probe_list_interfaces();
                    rc = EXIT_FAILURE;
                    goto done;
                }
                opt_interface = optarg;
                break;
            case 'l':
                cmd_probe_list_interfaces();
                rc = EXIT_SUCCESS;
                goto done;
            case 'r':
            {
                bool require_integrity = !strcmp("integrity", optarg);
                bool require_c13y = !strcmp("confidentiality", optarg);
                if (!(require_integrity || require_c13y)) {
                    loge("Unrecognised requirement: %s\n", optarg);
                    loge("Valid requirements:\n"
                                    "integrity\n"
                                    "confidentiality\n");
                    rc = EXIT_FAILURE;
                    goto done;
                }
                opt_require = optarg;
                break;
            }
        }
    }

    if (argc == optind) {
        rc = ast_ahb_bridge_probe(&ifaces);
        if (rc < 0) {
            bool denied = (rc == -EACCES || rc == -EPERM);
            if (denied && !priv_am_root()) {
                priv_print_unprivileged(name);
                rc = EXIT_FAILURE;
            } else if (rc == -ENOTSUP) {
                if (opt_require) {
                    logi("Probes failed, cannot access BMC AHB\n");
                    rc = EXIT_SUCCESS;
                } else {
                    loge("Probes failed, cannot access BMC AHB\n");
                    rc = EXIT_FAILURE;
                }
            } else {
                errno = -rc;
                perror("ast_ahb_bridge_probe");
                rc = EXIT_FAILURE;
            }
            goto done;
        }
    } else {
        struct ahb _ahb, *ahb = &_ahb;

        rc = ahb_init(ahb, ahb_debug, argv[optind + 0], argv[optind + 1],
                      strtoul(argv[optind + 2], NULL, 0), argv[optind + 3],
                      argv[optind + 4]);
        if (rc) {
            errno = -rc;
            perror("ahb_init");
            rc = EXIT_FAILURE;
            goto done;
        }

        rc = ast_ahb_bridge_discover(ahb, &ifaces);

        ahb_destroy(ahb);

        if (rc) {
            errno = -rc;
            perror("ast_ahb_bridge_discover");
            rc = EXIT_FAILURE;
            goto done;
        }
    }

    if (!opt_interface || !strcmp("ilpc", opt_interface)) {
        cmd_probe_pr_ip(ifaces.lpc.superio, "SuperIO: ");
        if (opt_require && !strcmp("confidentiality", opt_require))
            pass_requirement &= !(ifaces.lpc.superio == ip_state_enabled);

        if (ifaces.lpc.superio == ip_state_enabled) {
            cmd_probe_pr(ifaces.lpc.ilpc.rw, "Read-write", "Read-only",
                         "iLPC2AHB Bridge: ");
            if (opt_require && !strcmp("integrity", opt_require))
                pass_requirement &= !ifaces.lpc.ilpc.rw;
        }
    }
    if (!opt_interface
            || !strcmp("p2a", opt_interface)
            || !strcmp("xdma", opt_interface)) {
        cmd_probe_pr_ip(ifaces.pci.vga, "VGA PCIe device: ");
        if (ifaces.pci.vga == ip_state_enabled) {
            if (!opt_interface || !strcmp("p2a", opt_interface)) {
                cmd_probe_pr_ip(ifaces.pci.vga_mmio, "MMIO on VGA device: ");
                if (opt_require && !strcmp("confidentiality", opt_require))
                    pass_requirement &=
                        !(ifaces.pci.vga_mmio == ip_state_enabled);
                if (ifaces.pci.vga_mmio == ip_state_enabled) {
                    struct ahb_range *ranges[p2ab_ranges_max];

                    cmd_probe_sort_ranges(&ifaces.pci.ranges[0], ranges,
                                          p2ab_ranges_max);
                    printf("P2A write filter state:\n");
                    for (i = 0; i < p2ab_ranges_max; i++) {
                        struct ahb_range *r = ranges[i];

                        if (r->start == 0 && r->len == 0)
                            continue;

                        cmd_probe_pr(r->rw, "Read-write", "Read-only",
                                     "0x%08x-0x%08"PRIx64" (%s): ",
                                     r->start, (r->start + r->len - 1),
                                     r->name);

                        if (opt_require && !strcmp("integrity", opt_require))
                            pass_requirement &= !r->rw;
                    }
                }
            }
            if (!opt_interface || !strcmp("xdma", opt_interface))
                cmd_probe_pr_ip(ifaces.pci.vga_xdma, "X-DMA on VGA device: ");
        }
        cmd_probe_pr_ip(ifaces.pci.bmc, "BMC PCIe device: ");
        if (ifaces.pci.bmc == ip_state_enabled) {
            if (!opt_interface || !strcmp("p2a", opt_interface))
                cmd_probe_pr_ip(ifaces.pci.bmc_mmio, "MMIO on BMC device: ");
            if (!opt_interface || !strcmp("xdma", opt_interface))
                cmd_probe_pr_ip(ifaces.pci.bmc_xdma, "X-DMA on BMC device: ");
        }
        if ((ifaces.pci.vga == ip_state_enabled &&
                    ifaces.pci.vga_xdma == ip_state_enabled) ||
                (ifaces.pci.bmc == ip_state_enabled
                    && ifaces.pci.bmc_xdma == ip_state_enabled)) {
            if (!opt_interface || !strcmp("xdma", opt_interface))
                cmd_probe_pr(ifaces.xdma.unconstrained, "Yes", "No",
                             "X-DMA is unconstrained: ");
            if (opt_require)
                pass_requirement &= !ifaces.xdma.unconstrained;
        }
    }
    if (!opt_interface || !strcmp("debug", opt_interface)) {
        cmd_probe_pr_ip(ifaces.uart.debug, "Debug UART: ");
        if (opt_require)
            pass_requirement &= !(ifaces.uart.debug == ip_state_enabled);
        if (ifaces.uart.debug == ip_state_enabled) {
            printf("Debug UART enabled on: %s\n",
                   (ifaces.uart.uart == debug_uart1) ? "UART1" : "UART5");
        }
    }

    if (opt_require)
        rc = pass_requirement ? EXIT_SUCCESS : EXIT_FAILURE;
    else
        rc = EXIT_SUCCESS;

done:
    exit(rc);
}

struct command {
    const char *name;
    int (*fn)(const char *, int, char *[]);
};

static const struct command cmds[] = {
    { "probe", cmd_probe },
    { "devmem", cmd_devmem },
    { },
};

int main(int argc, char *argv[])
{
    const struct command *cmd = &cmds[0];

    if (argc < 2) {
        loge("Not enough arguments\n");
        help(argv[0]);
        exit(EXIT_FAILURE);
    }

    while (cmd->fn) {
        if (!strcmp(cmd->name, argv[1])) {
            if (!strcmp("probe", argv[1]))
                return cmd->fn(argv[0], argc - 1, argv + 1);
            else
                return cmd->fn(argv[0], argc - 2, argv + 2);
        }

        cmd++;
    }

    loge("Unknown command: %s\n", argv[1]);
    exit(EXIT_FAILURE);
}
