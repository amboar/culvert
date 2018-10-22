// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#define _GNU_SOURCE
#include "prompt.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int prompt_init(struct prompt *ctx, int fd, const char *eol, bool have_echo)
{
    ctx->stream = fdopen(fd, "r+");
    if (!ctx->stream)
        return -errno;

    ctx->eol = eol;

    ctx->have_echo = have_echo;

    return 0;
}

int prompt_destroy(struct prompt *ctx)
{
    int rc;

    rc = fclose(ctx->stream);
    if (rc < 0)
        return -errno;

    return 0;
}

int prompt_gets(struct prompt *ctx, char *output, size_t len)
{
    char *cursor;
    char *res;

    cursor = output;
    do {
        res = fgets(cursor, len, ctx->stream);
        if (!res)
            return -errno;
        cursor += strlen(res);
    } while (!strchr(output, '\n'));

    return 0;
}

int prompt_expect_into(struct prompt *ctx, const char *str, char *prior,
                         size_t len, char **prompt)
{
    ssize_t ingress;
    char *cursor;
    char *res;

    cursor = prior;
    do {
        ingress = read(fileno(ctx->stream), cursor, prior + len - cursor);
        if (ingress < 0)
            return -errno;

        cursor += ingress;
    } while(!(res = memmem(prior, cursor - prior, str, strlen(str))) &&
            len > (cursor - prior));

    if (prompt)
        *prompt = res;

    return res != NULL;
}

int prompt_expect(struct prompt *ctx, const char *str)
{
    char buf[100];
    int rc;

    do {
        rc = prompt_expect_into(ctx, str, &buf[0], sizeof(buf), NULL);
        if (rc < 0)
            return rc;
    } while(!rc);

    return rc;
}

ssize_t prompt_write(struct prompt *ctx, const char *buf, size_t len)
{
    const char *cursor;
    ssize_t egress;

    cursor = buf;
    do {
        egress = write(fileno(ctx->stream), cursor, buf + len - cursor);
        if (egress < 0)
            return -errno;

        cursor += egress;
    } while(cursor < (buf + len));

    return len;
}

ssize_t prompt_read(struct prompt *ctx, char *output, size_t len)
{
    char *cursor;
    ssize_t ingress;

    cursor = output;
    do {
        ingress = read(fileno(ctx->stream), cursor, output + len - cursor);
        if (ingress < 0)
            return -errno;

        cursor += ingress;
    } while(cursor < (output + len));

    return len;
}

int prompt_run(struct prompt *ctx, const char *cmd)
{
    size_t len = strlen(cmd);
    int rc;

    rc = prompt_write(ctx, cmd, len);
    if (rc < 0)
        return rc;

    rc = prompt_write(ctx, ctx->eol, strlen(ctx->eol));
    if (rc < 0)
        return rc;

    if (ctx->have_echo) {
        char *echo, *res;
        size_t eol_len = len + strlen(ctx->eol) + 1;

        echo = malloc(eol_len);
        if (!echo)
            return -errno;

        res = fgets(echo, eol_len, ctx->stream);
        free(echo);
        if (!res)
            return -errno;
    }

    return rc;
}

int prompt_expect_run(struct prompt *ctx, const char *prompt, const char *cmd)
{
    int rc;

    rc = prompt_expect(ctx, prompt);
    if (rc < 0)
        return rc;
    else if (!rc)
        return -EBADE;

    return prompt_run(ctx, cmd);
}

int prompt_run_expect(struct prompt *ctx, const char *cmd, const char *prompt,
                      char **output, size_t len)
{
    int rc;

    rc = prompt_run(ctx, cmd);
    if (rc < 0)
        return rc;

    return prompt_expect_into(ctx, prompt, *output, len, output);
}
