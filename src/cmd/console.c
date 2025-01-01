// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

#include "ahb.h"
#include "ast.h"
#include "cmd.h"
#include "compiler.h"
#include "host.h"
#include "log.h"
#include "priv.h"
#include "soc/clk.h"
#include "soc/uart/mux.h"
#include "uart/suart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int do_console(const char *name __unused, int argc, char *argv[])
{
    struct suart _suart, *suart = &_suart;
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    const char *user, *pass;
    struct uart_mux *mux;
    struct ahb *ahb;
    struct clk *clk;
    int cleanup;
    int baud;
    int rc;

    if (argc < 5) {
        loge("Not enough arguments for console command\n");
        exit(EXIT_FAILURE);
    }

    if (strcmp("uart3", argv[0])) {
        loge("Console only supports host on 'uart3'\n");
        exit(EXIT_FAILURE);
    }

    if (strcmp("uart2", argv[1])) {
        loge("Console only supports BMC on uart2\n");
        exit(EXIT_FAILURE);
    }

    baud = atoi(argv[2]);

    user = argv[3];
    pass = argv[4];


    if ((rc = host_init(host, argc - 5, argv + 5)) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        exit(EXIT_FAILURE);
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = EXIT_FAILURE;
        goto host_cleanup;
    }

    if ((rc = soc_probe(soc, ahb)) < 0) {
        errno = -rc;
        perror("soc_probe");
        goto host_cleanup;
    }

    if (!(clk = clk_get(soc))) {
        loge("Failed to acquire clock controller, exiting\n");
        goto soc_cleanup;
    }

    if (!(mux = uart_mux_get(soc))) {
        loge("Failed to acquire UART mux controller, exiting\n");
        goto soc_cleanup;
    }

    logi("Enabling UART clocks\n");
    /* Only 3 needs to be enabled as 1 and 2 are "reserved" for the host */
    if ((rc = clk_enable(clk, clk_uart3)) < 0) {
        errno = -rc;
        perror("clk_enable");
        goto soc_cleanup;
    }

    logi("Routing UART3 to UART5\n");
    if ((rc = uart_mux_route(mux, mux_obj_uart3, mux_obj_uart5)) < 0) {
        errno = -rc;
        perror("uart_mux_route");
        goto soc_cleanup;
    }

    logi("Initialising SUART3\n");
    rc = suart_init_defaults(suart, sio_suart3);
    if (rc) { errno = -rc; perror("suart_init"); goto mux_restore; }

    logi("Configuring baud rate of 115200 for BMC console\n");
    rc = suart_set_baud(suart, 115200);
    if (rc) { errno = -rc; perror("suart_set_baud"); goto suart_cleanup; }

    logi("Starting getty from BMC console\n");
    rc = suart_flush(suart, user, strlen(user));
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    rc = suart_flush(suart, "\n", 5);
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    sleep(3);

    rc = suart_flush(suart, pass, strlen(pass));
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    rc = suart_flush(suart, "\n", 8);
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    sleep(5);

    const char *run_getty = "/sbin/agetty -8 -L ttyS1 1200 xterm &\n";
    rc = suart_flush(suart, run_getty, strlen(run_getty));
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    /* We need to wait for the XMIT FIFO to clear before changing the UART
     * routing.
     *
     * TODO: Make this suck less by spinning on THRE
     */
    sleep(3);

    logi("Launched getty with: %s", run_getty);

    logi("Routing UARTs to connect UART3 with UART2\n");
    rc = uart_mux_restore(mux);
    if (rc) { errno = -rc; perror("uart_mux_restore"); goto suart_cleanup; }

    rc = uart_mux_connect(mux, mux_obj_uart3, mux_obj_uart2);
    if (rc) { errno = -rc; perror("uart_mux_connect"); goto suart_cleanup; }

    logi("Setting target baud rate of %d\n", baud);
    rc = suart_set_baud(suart, baud);
    if (rc) { errno = -rc; perror("suart_set_baud"); goto suart_cleanup; }

    rc = suart_flush(suart, "\n", 1);
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    sleep(5);

    rc = suart_flush(suart, user, strlen(user)); /* username */
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }
    rc = suart_flush(suart, "\n", 5);
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    sleep(3);

    rc = suart_flush(suart, pass, strlen(pass)); /* password */
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    rc = suart_flush(suart, "\n", 1);
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    rc = suart_run(suart, 0, 1);
    if (rc) { errno = -rc; perror("suart_run"); }

suart_cleanup:
    cleanup = suart_destroy(suart);
    if (cleanup) { errno = -cleanup; perror("suart_destroy"); }

mux_restore:
    cleanup = uart_mux_restore(mux);
    if (cleanup) { errno = -cleanup; perror("suart_destroy"); }

soc_cleanup:
    soc_destroy(soc);

host_cleanup:
    host_destroy(host);

    return rc;
}

static const struct cmd console_cmd = {
    "console",
    "HOST_UART BMC_UART BAUD USER PASSWORD",
    do_console
};
REGISTER_CMD(console_cmd);
