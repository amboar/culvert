// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ahb.h"
#include "ast.h"
#include "log.h"
#include "priv.h"
#include "uart/suart.h"
#include "uart/mux.h"

int cmd_console(const char *name, int argc, char *argv[])
{
    struct suart _suart, *suart = &_suart;
    struct uart_mux _mux, *mux = &_mux;
    struct ahb _ahb, *ahb = &_ahb;
    const char *user, *pass;
    uint32_t data;
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

    rc = ast_ahb_init(ahb, true);
    if (rc < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(name);
        } else if (rc == -ENOTSUP) {
            loge("Probes failed, cannot access BMC AHB\n");
        } else {
            errno = -rc;
            perror("ast_ahb_init");
        }
        exit(EXIT_FAILURE);
    }

    rc = uart_mux_init(mux, ahb);
    if (rc) { errno = -rc; perror("uart_mux_init"); goto ahb_cleanup; }

    logi("Routing UART3 to UART5\n");

    rc = uart_mux_route(mux, mux_obj_uart3, mux_obj_uart5);
    if (rc) { errno = -rc; perror("uart_mux_route"); goto ahb_cleanup; }

    rc = ahb_readl(ahb, 0x1e6e200c, &data);
    if (rc) { errno = -rc; perror("ahb_readl"); goto ahb_cleanup; }

    /* Clear UART3 clock stop bit */
    data &= ~(1 << 25);

    rc = ahb_writel(ahb, 0x1e6e200c, data);
    if (rc) { errno = -rc; perror("ahb_readl"); goto ahb_cleanup; }

    logi("Initialising SUART3\n");
    rc = suart_init_defaults(suart, sio_suart3);
    if (rc) { errno = -rc; perror("suart_init"); goto ahb_cleanup; }

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

    rc = suart_flush(suart, pass, strlen(pass)); /* passowrd */
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    rc = suart_flush(suart, "\n", 1);
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    rc = suart_run(suart, 0, 1);
    if (rc) { errno = -rc; perror("suart_run"); }

suart_cleanup:
    cleanup = suart_destroy(suart);
    if (cleanup) { errno = -cleanup; perror("suart_destroy"); }

    cleanup = uart_mux_restore(mux);
    if (cleanup) { errno = -cleanup; perror("suart_destroy"); }

ahb_cleanup:
    cleanup = ahb_destroy(ahb);
    if (cleanup) { errno = -cleanup; perror("ahb_destroy"); }

    return rc;
}

