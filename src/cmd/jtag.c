// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024 Sarah Maedel

#include "ahb.h"
#include "ast.h"
#include "compiler.h"
#include "host.h"
#include "log.h"
#include "priv.h"
#include "soc.h"
#include "soc/clk.h"
#include "soc/jtag.h"

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

static void
cmd_jtag_help(const char *name, int argc __unused, char *argv[] __unused)
{
        static const char *jtag_help =
                "Usage:\n"
                "%s jtag --help\n"
                "%s jtag --port OPENOCD-PORT ...\n"
                "%s jtag --target <arm|pcie|external>\n";

        printf(jtag_help, name, name, name, name);
}

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

int cmd_jtag(const char *name, int argc, char *argv[])
{
        struct host _host, *host = &_host;
        struct soc _soc, *soc = &_soc;
        struct ahb *ahb;
        struct jtag *jtag;
        int rc;
        uint32_t target_bits = SCU_JTAG_MASTER_TO_ARM;
        int port = 33333;
        const char *controller = "jtag";

        // getopt() expects the first argument to be a program name
        // somewhat ugly trick, but works
        argc += 1;
        argv -= 1;

        while (1) {
                int option_index = 0;
                int c;

                static struct option long_options[] = {
                        { "controller", required_argument, NULL, 'c' },
                        { "help", no_argument, NULL, 'h' },
                        { "port", required_argument, NULL, 'p' },
                        { "target", required_argument, NULL, 't' },
                        { },
                };

                c = getopt_long(argc, argv, "c:hp:t:", long_options, &option_index);
                if (c == -1)
                        break;

                switch (c) {
                        case 'c':
                                controller = optarg;
                                break;
                        case 'h':
                                cmd_jtag_help(name, argc, argv);
                                rc = EXIT_SUCCESS;
                                goto done;
                        case 'p':
                                port = atoi(optarg);
                                if (!port || port > UINT16_MAX) {
                                        loge("Invalid port '%s'\n", optarg);
                                        rc = EXIT_FAILURE;
                                        goto done;
                                }
                                break;
                        case 't':
                        {
                                if (!strcmp("arm", optarg)) {
                                        target_bits = SCU_JTAG_MASTER_TO_ARM;
                                } else if (!strcmp("pcie", optarg)) {
                                        target_bits = SCU_JTAG_MASTER_TO_PCIE;
                                } else if (!strcmp("external", optarg)) {
                                        target_bits = SCU_JTAG_NORMAL;
                                } else {
                                        loge("Unsupported JTAG target: '%s', valid targets:\n", optarg);
                                        loge("- arm:      Routes JTAG to the BMC internal ARM core\n");
                                        loge("- pcie:     Routes JTAG to the BMC internal PCIe PHY\n");
                                        loge("- external: Routes JTAG to the external GPIO pins (CPLDs, host CPUs, etc.)\n");
                                        rc = EXIT_FAILURE;
                                        goto done;

                                }
                                break;
                        }
                        case '?':
                                loge("Unknown command line option: %s\n", optopt);
                                rc = EXIT_FAILURE;
                                goto done;
                }
        }

        if ((rc = host_init(host, argc - optind, &argv[optind])) < 0) {
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
        if (!(jtag = jtag_get(soc, controller))) {
                loge("Failed to acquire JTAG controller, exiting\n");
                goto cleanup_soc;
        }

        jtag_route(jtag, target_bits);

        while (true) {
                run_openocd_bitbang_server(jtag, port);
        }

cleanup_soc:
        soc_destroy(soc);

cleanup_host:
        host_destroy(host);

done:
        exit(rc);
}
