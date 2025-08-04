// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024 Sarah Maedel

#include "ahb.h"
#include "ast.h"
#include "cmd.h"
#include "compiler.h"
#include "host.h"
#include "log.h"
#include "priv.h"
#include "soc.h"
#include "soc/clk.h"
#include "soc/jtag.h"

#include <argp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>

static char cmd_jtag_args_doc[] = "via DRIVER [INTERFACE [IP PORT USERNAME PASSWORD]]";
static char cmd_jtag_doc[] =
    "\n"
    "JTAG OpenOCD Bitbang server command"
    "\v"
    "Supported target types:\n"
    "  arm      Route JTAG to the internal ARM core\n"
    "  pcie     Route JTAG to the internal PCIe PHY\n"
    "  external Route JTAG to the external GPIO pins\n";

static struct argp_option cmd_jtag_options[] = {
    { "controller", 'c', "CTRL", 0, "JTAG controller to use (default: jtag)", 0 },
    { "port", 'p', "PORT", 0, "Port to listen on (default: 33333)", 0 },
    { "target-type", 't', "TYPE", 0, "JTAG target to route to (default: arm)", 0 },
    {0},
};

struct cmd_jtag_args
{
    struct connection_args connection;
    const char *controller;
    uint32_t target_bits;
    int listen_port;
};

static error_t cmd_jtag_parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmd_jtag_args *arguments = state->input;
    int rc;

    switch (key) {
    case 'c':
        arguments->controller = arg;
        break;
    case 'p':
        arguments->listen_port = atoi(arg);
        if (!arguments->listen_port && arguments->listen_port > UINT16_MAX)
            argp_error(state, "Invalid port '%s'", arg);
        break;
    case 't':
        if (!strcmp(arg, "arm"))
            arguments->target_bits = SCU_JTAG_MASTER_TO_ARM;
        else if (!strcmp(arg, "pcie"))
            arguments->target_bits = SCU_JTAG_MASTER_TO_PCIE;
        else if (!strcmp(arg, "external"))
            arguments->target_bits = SCU_JTAG_NORMAL;
        else
            argp_error(state, "Invalid target '%s'", arg);
        break;
    case ARGP_KEY_ARG:
        if (!strcmp(arg, "via")) {
            rc = cmd_parse_via(state->next - 1, state, &arguments->connection);
            if (rc != 0)
                argp_error(state, "Failed to parse connection arguments. Returned code %d", rc);
        }
        break;
    case ARGP_KEY_END:
        if (!arguments->controller)
            arguments->controller = "jtag";
        if (!arguments->listen_port)
            arguments->listen_port = 33333;
        if (!arguments->target_bits)
            arguments->target_bits = SCU_JTAG_MASTER_TO_ARM;

        if (arguments->connection.bridge_driver == NULL)
            argp_error(state, "Explicit driver must be defined");
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp cmd_jtag_argp = {
    .options = cmd_jtag_options,
    .parser = cmd_jtag_parse_opt,
    .args_doc = cmd_jtag_args_doc,
    .doc = cmd_jtag_doc,
};

static int run_openocd_bitbang_server(struct jtag *jtag, uint16_t port)
{
    int server_fd;
    int client_fd;
    struct sockaddr_in listen_addr;
    struct sockaddr_in client_addr;
    int opt = 1;

    if ((server_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        loge("socket() failed: %s\n", strerror(errno));
        return 1;
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listen_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        loge("bind() failed: %s\n", strerror(errno));
        return 1;
    }
    if (listen(server_fd, 5) < 0) {
        loge("listen() failed: %s\n", strerror(errno));
        return 1;
    }

    logi("Ready to accept OpenOCD remote_bitbang connection on 127.0.0.1:%u\n", port);

    while (true) {
        socklen_t addr_len = sizeof(client_addr);

        if ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len)) < 0) {
            loge("accept() failed: %s\n", strerror(errno));
            return 1;
        }

        logi("New connection from %s\n", inet_ntoa(client_addr.sin_addr));

        while (true) {
            int rc;
            uint8_t state;
            uint8_t tdo;
            uint8_t tdi;
            uint8_t tms;
            uint8_t tck;

            char command = 0;
            ssize_t n = read(client_fd, &command, 1);
            if (n <= 0) {
                loge("Client closed connection\n");
                close(server_fd);
                return 1;
            }

            switch (command) {
                /* LED blink commands */
                case 'B':
                case 'b':
                    break;
                    /* Read state */
                case 'R':
                    rc = jtag_bitbang_get(jtag, &tdo);
                    if (rc < 0) {
                        loge("jtag_bitbang_get() failed\n");
                        return 1;
                    }

                    // send ASCII 0 or 1 for TDO state
                    tdo += '0';
                    rc = write(client_fd, &tdo, 1);
                    if (rc < 0) {
                        loge("write(client_fd) failed: %d\n", rc);
                        return 1;
                    }
                    break;
                case 'Q':
                    logi("Received quit request from OpenOCD\n");
                    close(client_fd);
                    close(server_fd);
                    return 1;
                    /* Data requests */
                case '0'...'7':
                    state = command - '0';

                    tdi = !!(state & 1);
                    tms = !!(state & 2);
                    tck = !!(state & 4);

                    rc = jtag_bitbang_set(jtag, tck, tms, tdi);
                    if (rc < 0) {
                        loge("jtag_bitbang_set() failed: %d\n", rc);
                        return 1;
                    }
                    break;
                    /* Reset requests */
                case 'r':
                case 's':
                case 't':
                case 'u':
                    logt("Received reset request from OpenOCD, currently unsupported\n");
                    break;
                default:
                    loge("Received unknown command from OpenOCD: %c\n", command);
            }
        }
    }
}

static int do_jtag(int argc, char **argv)
{
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct ahb *ahb;
    struct jtag *jtag;
    int rc;

    struct cmd_jtag_args arguments = {0};
    rc = argp_parse(&cmd_jtag_argp, argc, argv, ARGP_IN_ORDER, 0, &arguments);
    if (rc != 0)
        return rc;

    if ((rc = host_init(host, &arguments.connection)) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        rc = EXIT_FAILURE;
        goto done;
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        exit(EXIT_FAILURE);
    }

    /* Probe the SoC */
    if ((rc = soc_probe(soc, ahb)) < 0) {
        errno = -rc;
        perror("soc_probe");
        goto cleanup_host;
    }

    /* Initialise the required SoC drivers */
    if (!(jtag = jtag_get(soc, arguments.controller))) {
        loge("Failed to acquire JTAG controller, exiting\n");
        goto cleanup_soc;
    }

    jtag_route(jtag, arguments.target_bits);

    while (true) {
        run_openocd_bitbang_server(jtag, arguments.listen_port);
    }

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

done:
    exit(rc);
}

static const struct cmd jtag_cmd = {
    .name = "jtag",
    .description = "Start a JTAG OpenOCD Bitbang server",
    .fn = do_jtag,
};
REGISTER_CMD(jtag_cmd);
