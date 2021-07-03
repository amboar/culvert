// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2020 IBM Corp.

#include "array.h"
#include "console.h"
#include "log.h"
#include "tty.h"

#include "ccan/container_of/container_of.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>

#define to_tty(console) container_of(console, struct tty, console)

struct baud_map {
    int baud;
    speed_t speed;
};

static const struct baud_map tty_baud_map[] = {
    { 1200, B1200 },
    { 115200, B115200 },
    { 0, B0 }, /* Sentinel */
};

static speed_t tty_find_speed(int baud) {
    int i;

    for (i = 0; tty_baud_map[i].baud && tty_baud_map[i].baud != baud; i++);

    return tty_baud_map[i].speed;
}

static int tty_set_baud(struct console *console, int baud)
{
    struct tty *ctx = to_tty(console);
    struct termios termios;
    speed_t speed;
    int rc;

    speed = tty_find_speed(baud);

    if (speed == B0) {
        loge("Unable to find matching speed for %d\n", baud);
        return -EINVAL;
    }

    /*
     * We do a sketchy borrow of the fd from the prompt to set the baud rate.
     * We cache the fd internally so no magic is required
     */
    rc = tcgetattr(ctx->fd, &termios);
    if (rc < 0) { return -errno; }

    rc = cfsetospeed(&termios, speed);
    if (rc < 0) { return -errno; }

    rc = cfsetispeed(&termios, speed);
    if (rc < 0) { return -errno; }

    rc = tcsetattr(ctx->fd, TCSADRAIN, &termios);
    if (rc < 0) { return -errno; }

    return 0;
}

static int tty_destroy(struct console *console)
{
    struct tty *ctx = to_tty(console);

    free(ctx);

    return 0;
}

static const struct console_ops tty_ops = {
    .destroy = tty_destroy,
    .set_baud = tty_set_baud,
};

/* Ownership of fd passes to the prompt associated with the debug instance */
int tty_init(struct tty *ctx, const char *path)
{
    struct termios termios;

    ctx->console.ops = &tty_ops;

    logi("Opening %s\n", path);

    ctx->fd = open(path, O_RDWR);
    if (!ctx->fd) {
        loge("Error opening %s: %d\n", path, strerror(errno));
        return -errno;
    }

    tcgetattr(ctx->fd, &termios);
    cfmakeraw(&termios);
    tcsetattr(ctx->fd, TCSAFLUSH, &termios);

    return ctx->fd;
}
