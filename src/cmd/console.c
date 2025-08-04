// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

#include "ahb.h"
#include "ast.h"
#include "cmd.h"
#include "compiler.h"
#include "connection.h"
#include "host.h"
#include "log.h"
#include "priv.h"
#include "soc/clk.h"
#include "soc/uart/mux.h"
#include "uart/suart.h"

#include <argp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char cmd_console_args_doc[] = "HOST_UART BMC_UART BAUD USER PASSWORD";
static char cmd_console_doc[] =
    "\n"
    "Console command"
    "\v"
    "Supported host consoles:\n"
    "  uart3\n"
    "\n"
    "Supported BMC consoles:\n"
    "  uart2\n";

struct cmd_console_args {
    const char *host_uart;
    const char *user;
    const char *pass;
    struct connection_args connection;
    int baud;
    /* 4 bytes of padding */
};

static struct argp_option cmd_console_options[] = {
    {0}
};

static error_t cmd_console_parse_opt(int key, char *arg,
                                     struct argp_state *state)
{
    struct cmd_console_args *arguments = state->input;

    switch (key) {
        case ARGP_KEY_ARG:
            switch (state->arg_num) {
                case 0:
                    if (strcmp("uart3", arg))
                        argp_error(state, "Console only supports host on 'uart3'");
                    arguments->host_uart = arg;
                    break;
                case 1:
                    if (strcmp("uart2", arg))
                        argp_error(state, "Console only supports BMC on 'uart2'");
                    arguments->connection.interface = arg;
                    break;
                case 2:
                    arguments->baud = atoi(arg);
                    break;
                case 3:
                    arguments->user = arg;
                    break;
                case 4:
                    arguments->pass = arg;
                    break;
            }
            break;
        case ARGP_KEY_END:
            if (state->arg_num < 5)
                argp_error(state, "Not enough arguments provided...");
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp cmd_console_argp = {
    .options = cmd_console_options,
    .parser = cmd_console_parse_opt,
    .args_doc = cmd_console_args_doc,
    .doc = cmd_console_doc,
};

static int do_console(int argc, char **argv)
{
    struct suart _suart, *suart = &_suart;
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    struct uart_mux *mux;
    struct ahb *ahb;
    struct clk *clk;
    int cleanup;
    int rc;

    struct cmd_console_args arguments = {0};
    rc = argp_parse(&cmd_console_argp, argc, argv, ARGP_IN_ORDER, 0, &arguments);
    if (rc != 0) {
        rc = EXIT_FAILURE;
        goto done;
    }

    if ((rc = host_init(host, &arguments.connection)) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        goto done;
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
    rc = suart_flush(suart, arguments.user, strlen(arguments.user));
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    rc = suart_flush(suart, "\n", 5);
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    sleep(3);

    rc = suart_flush(suart, arguments.pass, strlen(arguments.pass));
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

    logi("Setting target baud rate of %d\n", arguments.baud);
    rc = suart_set_baud(suart, arguments.baud);
    if (rc) { errno = -rc; perror("suart_set_baud"); goto suart_cleanup; }

    rc = suart_flush(suart, "\n", 1);
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    sleep(5);

    rc = suart_flush(suart, arguments.user, strlen(arguments.user)); /* username */
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }
    rc = suart_flush(suart, "\n", 5);
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    sleep(3);

    rc = suart_flush(suart, arguments.pass, strlen(arguments.pass)); /* password */
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

done:
    return rc;
}

static const struct cmd console_cmd = {
    .name = "console",
    .description = "Start a getty on the BMC console",
    .fn = do_console,
};
REGISTER_CMD(console_cmd);
