// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#define _GNU_SOURCE
#include "ast.h"
#include "debug.h"
#include "log.h"
#include "prompt.h"

#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static inline int streq(const char *a, const char *b)
{
    return !strcmp(a, b);
}

int debug_init(struct debug *ctx, const char *compatible, const char *ip,
               int port, const char *username, const char *password)
{
    struct sockaddr_in concentrator_addr, console_addr;
    int concentrator, console;
    int rc, cleanup;
    char *cmd;

    /*
     * Sanity-check presence of the password, though we also test again below
     * where we use it to avoid TOCTOU.
     */
    if (!getenv("AST_DEBUG_PASSWORD")) {
        loge("AST_DEBUG_PASSWORD environment variable is not defined\n");
        return -ENOTSUP;
    }

    if (!compatible || !streq("digi,portserver-ts-16", compatible))
        return -EINVAL;

    ctx->port = port;

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

    /* Use the "raw" port on the Digi portserver, which is 2000 + 100 */
    logi("Connecting to BMC console at %s:%d\n", ip, 2000 + 100 + port);
    console_addr = concentrator_addr;
    console_addr.sin_port = htons(2000 + 100 + port);
    console = socket(AF_INET, SOCK_STREAM, 0);
    if (console < 0) { goto cleanup_concentrator_prompt; }

    rc = connect(console, (struct sockaddr *)&console_addr,
                 sizeof(console_addr));
    if (rc < 0) { rc = -errno; goto cleanup_console; }

    rc = prompt_init(&ctx->console, console, "\r", false);
    if (rc < 0) { goto cleanup_console; };

    return 0;

cleanup_console:
    close(console);

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

int debug_cleanup(struct debug *ctx)
{
    char *cmd;
    int rc;

    logi("Reverting port %d to 115200 baud\n", ctx->port);
    rc = asprintf(&cmd, "set line range=%d baud=115200", ctx->port);
    if (rc < 0)
        return rc;

    rc = prompt_expect_run(&ctx->concentrator, "#> ", cmd);
    free(cmd);
    if (rc < 0)
        return rc;

    sleep(1);

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

    sleep(1);

    return rc;
}

int debug_destroy(struct debug *ctx)
{
    int rc = 0;

    rc |= prompt_destroy(&ctx->console);
    rc |= prompt_destroy(&ctx->concentrator);

    return rc ? -EBADF : 0;
}

int debug_enter(struct debug *ctx)
{
    const char *password;
    int cleanup;
    char *cmd;
    int rc;

    logi("Entering debug mode\n");
    rc = asprintf(&cmd, "set line range=%d baud=1200", ctx->port);
    if (rc < 0)
        return -errno;

    rc = prompt_expect_run(&ctx->concentrator, "#> ", cmd);
    free(cmd);
    if (rc < 0)
        return rc;

    sleep(1);

    password = getenv("AST_DEBUG_PASSWORD");
    if (!password) {
        loge("AST_DEBUG_PASSWORD environment variable is not defined\n");
        return -ENOTSUP;
    }
    rc = prompt_write(&ctx->console, password, strlen(password));
    if (rc < 0)
        goto cleanup_port_password;

    rc = prompt_expect(&ctx->console, "$ ");
    if (rc < 0)
        goto cleanup_port_password;

    rc = asprintf(&cmd, "set line range=%d baud=115200", ctx->port);
    if (rc < 0)
        goto cleanup_port_password;

    rc = prompt_expect_run(&ctx->concentrator, "#> ", cmd);
    free(cmd);
    if (rc < 0)
        goto cleanup_port_password;

    sleep(1);

    return 0;

cleanup_port_password:
    cleanup = asprintf(&cmd, "set line range=%d baud=115200", ctx->port);
    if (cleanup < 0)
        return -errno;

    prompt_expect_run(&ctx->concentrator, "#> ", cmd);
    free(cmd);

    sleep(1);

    prompt_run(&ctx->console, "");

    return rc;
}

int debug_exit(struct debug *ctx)
{
    int rc;

    logi("Exiting debug mode\n");
    rc = prompt_run(&ctx->console, "q");
    if (rc < 0)
        return rc;

    sleep(1);

    return prompt_run(&ctx->console, "");
}

int debug_probe(struct debug *ctx)
{
    int rc;

    rc = debug_enter(ctx);
    if (rc < 0)
        return rc;

    return debug_exit(ctx);
}

static ssize_t debug_parse_d(char *line, char *buf)
{
    char *eoa, *words, *token, *stream;
    char *saveptr;
    char *cursor;
    ssize_t rc;

    /* Strip leading address */
    eoa = strchr(line, ':');
    if (!eoa)
        return -EBADE;

    /* Extract 4-byte values */
    words = eoa + 1;
    cursor = buf;
    stream = strdup(words);
    while ((token = strtok_r(words, " ", &saveptr))) {
        rc = sscanf(token, "%02hhx%02hhx%02hhx%02hhx",
                    &cursor[3], &cursor[2], &cursor[1], &cursor[0]);
        if (rc < 0) {
            rc = -errno;
            goto done;
        }

        if (rc < 4) {
            loge("Wanted 4 but extracted %zd bytes from token '%s' in words '%s' from line '%s'\n",
                 rc, token, words, line);
            rc = -EBADE;
            goto done;
        }

        cursor += rc;
        words = NULL;
    }

    rc = cursor - buf;

done:
    free(stream);
    return rc;
}

static int debug_read_fixed(struct debug *ctx, char mode, uint32_t phys,
                            uint32_t *val)
{
    char buf[100], *response, *prompt;
    char *command;
    unsigned long parsed;
    int rc;

    if (!(mode == 'i' || mode == 'r'))
        return -EINVAL;

    rc = asprintf(&command, "%c %x", mode, phys);
    if (rc < 0)
        return -errno;

    prompt = &buf[0];
    rc = prompt_run_expect(&ctx->console, command, "$ ", &prompt, sizeof(buf));
    free(command);
    if (rc < 0)
        return rc;

    /* Terminate the string by overwriting the prompt */
    *prompt = '\0';

    /* Discard echoed response */
    response = strchr(buf, ctx->console.eol[0]);
    if (!response)
        return -EIO;

    /* Extract the data */
    errno = 0;
    parsed = strtoul(response, NULL, 16);
    if (errno == ERANGE && (parsed == LONG_MAX || parsed == LONG_MIN))
        return -errno;

    *val = parsed;

    return 0;
}

#define DEBUG_D_MAX_LEN (128 * 1024)

ssize_t debug_read(struct debug *ctx, uint32_t phys, void *buf, size_t len)
{
    char line[2 * sizeof("20002ba0:31e01002 20433002 30813003 e1a06002\r\n")];
    size_t remaining = len;
    size_t ingress;
    char *command;
    char *cursor;
    ssize_t rc;

    if (len < 4) {
        uint32_t val = 0;

        cursor = buf;
        while (remaining) {
            rc = debug_read_fixed(ctx, 'i', phys, &val);
            if (rc < 0)
                return rc;

            *cursor++ = val & 0xff;
            remaining--;
            phys++;
        }

        return len;
    }

    cursor = buf;
    do {
        size_t consumed;
        int found;

retry:
        ingress = remaining > DEBUG_D_MAX_LEN ? DEBUG_D_MAX_LEN : remaining;

        rc = asprintf(&command, "d %x %zx", phys, ingress);
        if (rc < 0)
            return -errno;

        rc = prompt_run(&ctx->console, command);
        free(command);
        if (rc < 0)
            return rc;

        /* Eat the echoed command */
        do {
            found = prompt_gets(&ctx->console, line, sizeof(line));
            if (found < 0)
                return found;
        } while (!strcmp("$ \n", line)); /* Deal any prompt from a prior run */

        consumed = 0;
        do {
            found = prompt_gets(&ctx->console, line, sizeof(line));
            if (found < 0)
                return found;

            rc = debug_parse_d(line, cursor);
            if (rc < 0) {
                rc = prompt_run(&ctx->console, "");
                if (rc < 0)
                    return rc;
                rc = prompt_expect(&ctx->console, "$ ");
                if (rc < 0)
                    return rc;
                loge("Failed to parse line '%s'\n", line);
                loge("Retrying from address 0x%"PRIx32"\n", phys);
                goto retry;
            }

            cursor += rc;
            consumed += rc;
        } while (consumed < ingress);

        /*
         * Normally we would prompt_expect() here, but prompt_gets() has likely
         * swallowed the prompt, so we'll YOLO and just assume it's done.
         */

        phys += ingress;
        remaining -= ingress;
    } while(remaining);

    return len;
}

#define DEBUG_CMD_U_MAX 128

ssize_t debug_write(struct debug *ctx, uint32_t phys, const void *buf, size_t len)
{
    const void *cursor;
    size_t remaining;
    size_t egress;
    char *command;
    char mode;
    int rc;

    if (len <= 4) {
        size_t remaining;

        remaining = len;
        cursor = buf;
        while (remaining) {
            rc = asprintf(&command, "o %x %hhx", phys, *(const uint8_t *)cursor);
            if (rc < 0)
                return -errno;

            rc = prompt_run(&ctx->console, command);
            if (rc < 0)
                return rc;

            rc = prompt_expect(&ctx->console, "$ ");
            if (rc < 0)
                return rc;
            if (rc == 0)
                return -EINVAL;

            cursor++;
            phys++;
            remaining--;
        }

        return len;
    }

    mode = 'u';

    remaining = len;
    cursor = buf;
    do {
        egress = remaining > DEBUG_CMD_U_MAX ? DEBUG_CMD_U_MAX : remaining;

        rc = asprintf(&command, "%c %x %zx", mode, phys, egress);
        if (rc < 0)
            return -errno;

        rc = prompt_run(&ctx->console, command);
        free(command);
        if (rc < 0)
            return rc;

        rc = prompt_write(&ctx->console, (const char *)cursor, egress);
        if (rc < 0)
            return rc;

        rc = prompt_expect(&ctx->console, "$ ");
        if (rc < 0)
            return rc;

        phys += egress;
        cursor += egress;
        remaining -= egress;
    } while (remaining);

    return len;
}

int debug_readl(struct debug *ctx, uint32_t phys, uint32_t *val)
{
    return debug_read_fixed(ctx, 'r', phys, val);
}

int debug_writel(struct debug *ctx, uint32_t phys, uint32_t val)
{
    char *command;
    int rc;

    rc = asprintf(&command, "w %x %x", phys, val);
    if (rc < 0)
        return -errno;

    rc = prompt_run(&ctx->console, command);
    if (rc < 0)
        return rc;

    if (!((phys & ~0x20) == (AST_G5_WDT | WDT_RELOAD) && val == 0)) {
        rc = prompt_expect(&ctx->console, "$ ");
        if (rc < 0)
            return rc;
        if (rc == 0)
            return -EINVAL;
    }

    return 0;
}
