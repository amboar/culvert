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

#include "ahb.h"
#include "ast.h"
#include "clk.h"
#include "debug.h"
#include "devmem.h"
#include "flash.h"
#include "ilpc.h"
#include "l2a.h"
#include "log.h"
#include "lpc.h"
#include "otp.h"
#include "p2a.h"
#include "mb.h"
#include "priv.h"
#include "prompt.h"
#include "sfc.h"
#include "sio.h"
#include "uart/suart.h"
#include "uart/mux.h"
#include "uart/vuart.h"
#include "wdt.h"

int cmd_ilpc(const char *name, int argc, char *argv[]);
int cmd_p2a(const char *name, int argc, char *argv[]);
int cmd_debug(const char *name, int argc, char *argv[]);
int cmd_devmem(const char *name, int argc, char *argv[]);
int cmd_console(const char *name, int argc, char *argv[]);
int cmd_read(const char *name, int argc, char *argv[]);
int cmd_write(const char *name, int argc, char *argv[]);
int cmd_replace(const char *name, int argc, char *argv[]);
int cmd_probe(const char *name, int argc, char *argv[]);
int cmd_reset(const char *name, int argc, char *argv[]);
int cmd_sfc(const char *name, int argc, char *argv[]);

static void help(const char *name)
{
    printf("%s: " VERSION "\n", name);
    printf("Usage:\n");
    printf("\n");
    printf("%s probe [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s ilpc read ADDRESS\n", name);
    printf("%s ilpc write ADDRESS VALUE\n", name);
    printf("%s p2a vga read ADDRESS\n", name);
    printf("%s p2a vga write ADDRESS VALUE\n", name);
    printf("%s debug read ADDRESS INTERFACE [IP PORT USERNAME PASSWORD]\n", name);
    printf("%s debug write ADDRESS VALUE INTERFACE [IP PORT USERNAME PASSWORD]\n", name);
    printf("%s devmem read ADDRESS\n", name);
    printf("%s devmem write ADDRESS VALUE\n", name);
    printf("%s console HOST_UART BMC_UART BAUD USER PASSWORD\n", name);
    printf("%s read firmware [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s read ram [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s write firmware [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s replace ram MATCH REPLACE\n", name);
    printf("%s reset TYPE [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s sfc fmc read ADDRESS LENGTH [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s sfc fmc erase ADDRESS LENGTH [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s sfc fmc write ADDRESS LENGTH [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s otp read conf [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s otp read strap [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s otp write strap BIT VALUE [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s otp write conf WORD BIT [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
}

int cmd_otp(const char *name, int argc, char *argv[])
{
    enum otp_region reg = otp_region_conf;
    struct ahb _ahb, *ahb = &_ahb;
    struct otp _otp, *otp = &_otp;
    bool rd = true;
    int argo = 2;
    int cleanup;
    int rc;

    if (argc < 2) {
        loge("Not enough arguments for otp command\n");
        help(name);
        exit(EXIT_FAILURE);
    }

    if (!strcmp("conf", argv[1]))
        reg = otp_region_conf;
    else if (!strcmp("strap", argv[1]))
        reg = otp_region_strap;
    else {
        loge("Unsupported otp region: %s\n", argv[1]);
        help(name);
        exit(EXIT_FAILURE);
    }

    if (!strcmp("write", argv[0])) {
        rd = false;
        argo += 2;
    } else if (strcmp("read", argv[0])) {
        loge("Unsupported command: %s\n", argv[0]);
        help(name);
        exit(EXIT_FAILURE);
    }

    if (argc < argo) {
        loge("Not enough arguments for otp command\n");
        help(name);
        exit(EXIT_FAILURE);
    }

    rc = ast_ahb_from_args(ahb, argc - argo, &argv[argo]);
    if (rc < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(name);
        } else if (rc == -ENOTSUP) {
            loge("Probes failed, cannot access BMC AHB\n");
        } else {
            errno = -rc;
            perror("ast_ahb_from_args");
        }
        exit(EXIT_FAILURE);
    }

    rc = otp_init(otp, ahb);
    if (rc < 0) {
        errno = -rc;
        perror("otp_init");
        cleanup = ahb_destroy(ahb);
        exit(EXIT_FAILURE);
    }

    if (rd)
        rc = otp_read(otp, reg);
    else {
        if (reg == otp_region_strap) {
            unsigned int bit;
            unsigned int val;

            bit = strtoul(argv[2], NULL, 0);
            val = strtoul(argv[3], NULL, 0);

            rc = otp_write_strap(otp, bit, val);
        } else {
            unsigned int word;
            unsigned int bit;

            word = strtoul(argv[2], NULL, 0);
            bit = strtoul(argv[3], NULL, 0);

            rc = otp_write_conf(otp, word, bit);
        }
    }

    cleanup = ahb_destroy(ahb);
    if (cleanup < 0) { errno = -cleanup; perror("ahb_destroy"); }

    return rc;
}

struct command {
    const char *name;
    int (*fn)(const char *, int, char *[]);
};

static const struct command cmds[] = {
    { "ilpc", cmd_ilpc },
    { "p2a", cmd_p2a },
    { "console", cmd_console },
    { "read", cmd_read },
    { "write", cmd_write },
    { "replace", cmd_replace },
    { "probe", cmd_probe },
    { "debug", cmd_debug },
    { "reset", cmd_reset },
    { "devmem", cmd_devmem },
    { "sfc", cmd_sfc },
    { "otp", cmd_otp },
    { },
};

int main(int argc, char *argv[])
{
    const struct command *cmd = &cmds[0];
    bool show_help = false;
    int verbose = 0;

    while (1) {
        static struct option long_options[] = {
            { "help", no_argument, NULL, 'h' },
            { "verbose", no_argument, NULL, 'v' },
            { },
        };
        int option_index = 0;
        int c;

        c = getopt_long(argc, argv, "+hv", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'h':
                show_help = true;
                break;
            case 'v':
                verbose++;
                break;
            default:
                continue;
        }
    }

    if (optind == argc) {
        if (!show_help)
            loge("Not enough arguments\n");
        help(argv[0]);
        exit(EXIT_FAILURE);
    }

    if ((level_info + verbose) <= level_trace) {
        log_set_level(level_info + verbose);
    } else {
        log_set_level(level_trace);
    }

    while (cmd->fn) {
        if (!strcmp(cmd->name, argv[optind])) {
            int offset = optind;

            /* probe uses getopt, but for subcommands not using getopt */
            if (strcmp("probe", argv[optind])) {
                offset += 1;
            }
            optind = 1;

            return cmd->fn(argv[0], argc - offset, argv + offset);
        }

        cmd++;
    }

    loge("Unknown command: %s\n", argv[1]);
    exit(EXIT_FAILURE);
}
