// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#define _GNU_SOURCE
#include "ast.h"
#include "bridge.h"
#include "console.h"
#include "debug.h"
#include "log.h"
#include "prompt.h"
#include "ts16.h"
#include "tty.h"

#include "ccan/container_of/container_of.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define to_debug(ahb) container_of(ahb, struct debug, ahb)

static inline int streq(const char *a, const char *b)
{
    return !strcmp(a, b);
}

int debug_enter(struct debug *ctx)
{
    const char *password;
    int rc;

    logi("Entering debug mode\n");

    password = getenv("AST_DEBUG_PASSWORD");
    if (!password) {
        loge("AST_DEBUG_PASSWORD environment variable is not defined\n");
        return -ENOTSUP;
    }

    rc = console_set_baud(ctx->console, 1200);
    if (rc < 0)
        return rc;

    rc = prompt_write(&ctx->prompt, password, strlen(password));
    if (rc < 0)
        goto cleanup_port_password;

    rc = prompt_expect(&ctx->prompt, "$ ");
    if (rc < 0)
        goto cleanup_port_password;

    rc = console_set_baud(ctx->console, 115200);
    if (!rc)
        sleep(1);

    return rc;

cleanup_port_password:
    console_set_baud(ctx->console, 115200);

    prompt_run(&ctx->prompt, "");

    return rc;
}

int debug_exit(struct debug *ctx)
{
    int rc;

    logi("Exiting debug mode\n");
    rc = prompt_run(&ctx->prompt, "q");
    if (rc < 0)
        return rc;

    sleep(1);

    prompt_run(&ctx->prompt, "");

    return console_set_baud(ctx->console, 115200);
}

int debug_probe(struct debug *ctx)
{
    int rc;

    logd("Probing %s\n", ctx->ahb.drv->name);

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
    rc = prompt_run_expect(&ctx->prompt, command, "$ ", &prompt, sizeof(buf));
    free(command);
    if (rc < 0)
        return rc;

    /* Terminate the string by overwriting the prompt */
    *prompt = '\0';

    /* Discard echoed response */
    response = strchr(buf, ctx->prompt.eol[0]);
    if (!response)
        return -EIO;

    /* Extract the data */
    errno = 0;
    parsed = strtoul(response, NULL, 16);
    if (errno == ERANGE && (parsed == ULONG_MAX))
        return -errno;

    *val = parsed;

    return 0;
}

#define DEBUG_D_MAX_LEN (128 * 1024)

ssize_t debug_read(struct ahb *ahb, uint32_t phys, void *buf, size_t len)
{
    char line[2 * sizeof("20002ba0:31e01002 20433002 30813003 e1a06002\r\n")];
    struct debug *ctx = to_debug(ahb);
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

        rc = prompt_run(&ctx->prompt, command);
        free(command);
        if (rc < 0)
            return rc;

        /* Eat the echoed command */
        do {
            found = prompt_gets(&ctx->prompt, line, sizeof(line));
            if (found < 0)
                return found;
        } while (!strcmp("$ \n", line)); /* Deal any prompt from a prior run */

        consumed = 0;
        do {
            found = prompt_gets(&ctx->prompt, line, sizeof(line));
            if (found < 0)
                return found;

            rc = debug_parse_d(line, cursor);
            if (rc < 0) {
                rc = prompt_run(&ctx->prompt, "");
                if (rc < 0)
                    return rc;
                rc = prompt_expect(&ctx->prompt, "$ ");
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

ssize_t debug_write(struct ahb *ahb, uint32_t phys, const void *buf, size_t len)
{
    struct debug *ctx = to_debug(ahb);
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

            rc = prompt_run(&ctx->prompt, command);
            if (rc < 0)
                return rc;

            rc = prompt_expect(&ctx->prompt, "$ ");
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

        rc = prompt_run(&ctx->prompt, command);
        free(command);
        if (rc < 0)
            return rc;

        rc = prompt_write(&ctx->prompt, (const char *)cursor, egress);
        if (rc < 0)
            return rc;

        rc = prompt_expect(&ctx->prompt, "$ ");
        if (rc < 0)
            return rc;

        phys += egress;
        cursor += egress;
        remaining -= egress;
    } while (remaining);

    return len;
}

int debug_readl(struct ahb *ahb, uint32_t phys, uint32_t *val)
{
    struct debug *ctx = to_debug(ahb);
    return debug_read_fixed(ctx, 'r', phys, val);
}

int debug_writel(struct ahb *ahb, uint32_t phys, uint32_t val)
{
    struct debug *ctx = to_debug(ahb);
    char *command;
    int rc;

    rc = asprintf(&command, "w %x %x", phys, val);
    if (rc < 0)
        return -errno;

    rc = prompt_run(&ctx->prompt, command);
    if (rc < 0)
        return rc;

    /* XXX: This kludge is super annoying */
#define AST_G5_WDT	0x1e785000
#define   WDT_RELOAD	0x04
    if (!((phys & ~0x20) == (AST_G5_WDT | WDT_RELOAD) && val == 0)) {
#undef AST_G5_WDT
#undef    WDT_RELOAD
        rc = prompt_expect(&ctx->prompt, "$ ");
        if (rc < 0)
            return rc;
        if (rc == 0)
            return -EINVAL;
    }

    return 0;
}

static int ts16_console_init(struct debug *ctx, va_list args)
{
    const char *ip, *username, *password;
    struct ts16 *ts16;
    int port;
    int fd;

    ts16 = malloc(sizeof(*ts16));
    if (!ts16)
        return -ENOMEM;

    ip = va_arg(args, const char *);
    port = va_arg(args, int);
    username = va_arg(args, const char *);
    password = va_arg(args, const char *);

    fd = ts16_init(ts16, ip, port, username, password);
    if (fd < 0) { goto cleanup_free; }

    ctx->console = &ts16->console;

    return fd;

cleanup_free:
    free(ts16);

    return fd;
}

static int tty_console_init(struct debug *ctx, const char *path)
{
    struct tty *tty;
    int fd;

    tty = malloc(sizeof(*tty));
    if (!tty)
        return -ENOMEM;

    fd = tty_init(tty, path);
    if (fd < 0) { goto cleanup_free; }

    ctx->console = &tty->console;

    return fd;

cleanup_free:
    free(tty);

    return fd;
}

int debug_init(struct debug *ctx, ...)
{
    va_list args;
    int rc;

    va_start(args, ctx);
    rc = debug_init_v(ctx, args);
    va_end(args);

    return rc;
}

static const struct ahb_ops debug_ahb_ops = {
    .read = debug_read,
    .write = debug_write,
    .readl = debug_readl,
    .writel = debug_writel
};

static struct ahb *debug_driver_probe(int argc, char *argv[]);
static void debug_driver_destroy(struct ahb *ahb);

static struct bridge_driver debug_driver = {
    .name = "debug-uart",
    .probe = debug_driver_probe,
    .destroy = debug_driver_destroy,
};
REGISTER_BRIDGE_DRIVER(debug_driver);

int debug_init_v(struct debug *ctx, va_list args)
{
    const char *interface;
    int rc, fd;

    /*
     * Sanity-check presence of the password, though we also test again below
     * where we use it to avoid TOCTOU.
     */
    if (!getenv("AST_DEBUG_PASSWORD")) {
        loge("AST_DEBUG_PASSWORD environment variable is not defined\n");
        return -ENOTSUP;
    }

    interface = va_arg(args, const char *);

    if (!interface)
        return -EINVAL;

    if (streq("digi,portserver-ts-16", interface)) {
        fd = ts16_console_init(ctx, args);
    } else {
        fd = tty_console_init(ctx, interface);
    }

    if (fd < 0) {
        loge("Failed to initialise the console (%s): %d\n", interface, fd);
        return fd;
    }

    rc = prompt_init(&ctx->prompt, fd, "\r", false);
    if (rc < 0) { goto cleanup_ts16; }

    ahb_init_ops(&ctx->ahb, &debug_driver, &debug_ahb_ops);

    return 0;

cleanup_ts16:
    console_destroy(ctx->console);

    return rc;
}

int debug_destroy(struct debug *ctx)
{
    int rc = 0;

    rc |= prompt_destroy(&ctx->prompt);
    rc |= console_destroy(ctx->console);

    return rc ? -EBADF : 0;
}

static struct ahb *debug_driver_probe(int argc, char *argv[])
{
    struct debug *ctx;
    int rc;

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return NULL;
    }

    if (argc == 1) {
        /* Local debug interface */
        if ((rc = debug_init(ctx, argv[0])) < 0) {
            loge("Failed to initialise local debug interace on %s: %d\n",
                    argv[0], rc);
            goto cleanup_ctx;
        }
    } else if (argc == 5) {
        /* Remote debug interface */
        rc = debug_init(ctx, argv[0], argv[1], strtoul(argv[2], NULL, 0),
                        argv[3], argv[4]);
        if (rc < 0) {
            loge("Failed to initialise remote debug interface: %d\n", rc);
            goto cleanup_ctx;
        }
    } else {
        logd("Unrecognised argument list for debug interface (%d)\n", argc);
        return NULL;
    }

    if ((rc = debug_enter(ctx)) < 0) {
        loge("Failed to enter debug UART context: %d\n", rc);
        goto destroy_ctx;
    }

    return debug_as_ahb(ctx);

destroy_ctx:
    debug_destroy(ctx);

cleanup_ctx:
    free(ctx);

    return NULL;
}

static void debug_driver_destroy(struct ahb *ahb)
{
    struct debug *ctx = to_debug(ahb);
    int rc;

    if ((rc = debug_exit(ctx)) < 0) {
        loge("Failed to exit debug UART context: %d\n", rc);
    }

    if ((rc = debug_destroy(ctx)) < 0) {
        loge("Failed to destroy debug bridge: %d\n", rc);
    }

    free(ctx);
}
