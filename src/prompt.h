/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _PROMPT_H
#define _PROMPT_H

#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

struct prompt {
    FILE *stream;
    const char *eol;
    bool have_echo;
};

/**
 * @param fd Owned, closed on prompt_destroy(), must be mode "r+"
 */
int prompt_init(struct prompt *ctx, int fd, const char *eol, bool have_echo);
int prompt_destroy(struct prompt *ctx);

int prompt_expect(struct prompt *ctx, const char *str);
int prompt_expect_into(struct prompt *ctx, const char *str, char *prior,
			 size_t len, char **prompt);
ssize_t prompt_write(struct prompt *ctx, const char *cmd, size_t len);
ssize_t prompt_read(struct prompt *ctx, char *output, size_t len);
int prompt_gets(struct prompt *ctx, char *output, size_t len);

int prompt_run(struct prompt *ctx, const char *cmd);
int prompt_expect_run(struct prompt *ctx, const char *prompt, const char *cmd);
int prompt_run_expect(struct prompt *ctx, const char *cmd, const char *prompt,
		      char **output, size_t len);

#endif
