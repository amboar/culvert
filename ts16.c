// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2020 IBM Corp.

#define _GNU_SOURCE
#include "log.h"
#include "prompt.h"
#include "ts16.h"

#include "ccan/container_of/container_of.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define to_ts16(console) container_of(console, struct ts16, console)

static int ts16_control_init(struct ts16 *ctx, const char *ip, int port,
                             const char *username, const char *password)
{
    struct sockaddr_in concentrator_addr;
    int concentrator;
    int rc, cleanup;
    char *cmd;

    logi("Connecting to Digi Portserver TS 16 at %s:%d\n", ip, 23);
    concentrator_addr.sin_family = AF_INET;
    concentrator_addr.sin_port = htons(23);
    if (inet_aton(ip, &concentrator_addr.sin_addr) == 0)
        return -errno;

    concentrator = socket(AF_INET, SOCK_STREAM, 0);
    if (concentrator < 0) { return -errno; }

    rc = connect(concentrator, (struct sockaddr *)&concentrator_addr,
                 sizeof(concentrator_addr));
    if (rc < 0) { rc = -errno; goto cleanup_concentrator; }

    rc = prompt_init(&ctx->concentrator, concentrator, "\r\n", false);
    if (rc < 0) { goto cleanup_concentrator; };

    logi("Logging into Digi Portserver TS\n");
    rc = prompt_expect_run(&ctx->concentrator, "login: ", username);
    if (rc < 0) { rc = -errno; goto cleanup_concentrator_prompt; }

    rc = prompt_expect_run(&ctx->concentrator, "password: ", password);
    if (rc < 0) { rc = -errno; goto cleanup_concentrator_prompt; }

    logi("Configuring binary mode on port %d\n", ctx->port);
    rc = asprintf(&cmd, "set port range=%d bin=on", ctx->port);
    if (rc < 0) { goto cleanup_concentrator_prompt; }

    rc = prompt_expect_run(&ctx->concentrator, "#> ", cmd);
    free(cmd);
    if (rc < 0) { goto cleanup_concentrator_prompt; }

    logi("Resetting port %d\n", ctx->port);
    rc = asprintf(&cmd, "kill tty=%d", ctx->port);
    if (rc < 0) { goto cleanup_port; }

    rc = prompt_expect_run(&ctx->concentrator, "#> ", cmd);
    free(cmd);
    if (rc < 0) { goto cleanup_port; }

    sleep(1);

    return 0;

cleanup_port:
    logi("Disabling binary mode on port %d\n", ctx->port);
    cleanup = asprintf(&cmd, "set port range=%d bin=off", ctx->port);
    if (cleanup < 0 && !rc) { rc = cleanup; };

    cleanup = prompt_expect_run(&ctx->concentrator, "#> ", cmd);
    free(cmd);
    if (cleanup < 0 && !rc) { rc = cleanup; };

    logi("Resetting port %d\n", ctx->port);
    cleanup = asprintf(&cmd, "kill tty=%d", ctx->port);
    if (cleanup < 0 && !rc) { rc = cleanup; };

    cleanup = prompt_expect_run(&ctx->concentrator, "#> ", cmd);
    free(cmd);
    if (cleanup < 0 && !rc) { rc = cleanup; };

    sleep(1);

cleanup_concentrator_prompt:
    cleanup = prompt_destroy(&ctx->concentrator);
    if (cleanup < 0 && !rc) { rc = cleanup; }

cleanup_concentrator:
    close(concentrator);

    return rc;
}

static int ts16_control_destroy(struct ts16 *ctx)
{
    char *cmd;
    int rc;

    logi("Disabling binary mode on port %d\n", ctx->port);
    rc = asprintf(&cmd, "set port range=%d bin=off", ctx->port);
    if (rc < 0)
        return -errno;

    rc = prompt_expect_run(&ctx->concentrator, "#> ", cmd);
    free(cmd);
    if (rc < 0)
        return rc;

    logi("Resetting port %d\n", ctx->port);
    rc = asprintf(&cmd, "kill tty=%d", ctx->port);
    if (rc < 0)
        return rc;

    rc = prompt_expect_run(&ctx->concentrator, "#> ", cmd);
    free(cmd);
    if (rc < 0)
        return rc;

    sleep(1);

    return prompt_destroy(&ctx->concentrator);
}

static int ts16_console_init(struct ts16 *ctx, const char *ip, int port)
{
    struct sockaddr_in console_addr;
    int console;
    int rc;

    /* Use the "raw" port on the Digi portserver, which is 2000 + 100 */
    logi("Connecting to BMC console at %s:%d\n", ip, 2000 + 100 + port);
    console_addr.sin_family = AF_INET;
    console_addr.sin_port = htons(2000 + 100 + port);
    if (inet_aton(ip, &console_addr.sin_addr) == 0)
        return -errno;

    console = socket(AF_INET, SOCK_STREAM, 0);
    if (console < 0) { return console; }

    rc = connect(console, (struct sockaddr *)&console_addr,
                 sizeof(console_addr));
    if (rc < 0) { rc = -errno; goto cleanup_console; }

    return console;

cleanup_console:
    close(console);

    return rc;
}

static int ts16_set_baud(struct console *console, int baud)
{
    struct ts16 *ctx = to_ts16(console);
    char *cmd;
    int rc;

    rc = asprintf(&cmd, "set line range=%d baud=%d", ctx->port, baud);
    if (rc < 0)
        return -errno;

    rc = prompt_expect_run(&ctx->concentrator, "#> ", cmd);
    free(cmd);
    if (rc < 0)
        return rc;

    sleep(1);

    return 0;
}

static int ts16_destroy(struct console *console)
{
    struct ts16 *ctx = to_ts16(console);

    ts16_control_destroy(ctx);

    free(ctx);

    return 0;
}

static const struct console_ops ts16_ops = {
    .set_baud = ts16_set_baud,
    .destroy = ts16_destroy,
};

int ts16_init(struct ts16 *ctx, const char *ip, int port, const char *username,
              const char *password)
{
    int rc, fd;

    ctx->port = port;
    ctx->console.ops = &ts16_ops;

    rc = ts16_control_init(ctx, ip, port, username, password);
    if (rc < 0)
        return rc;

    fd = ts16_console_init(ctx, ip, port);
    if (fd < 0)
        ts16_control_destroy(ctx);

    return fd;
}
