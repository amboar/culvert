/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef SUART_H
#define SUART_H

#include <stdint.h>
#include <sys/types.h>

#include "../lpc.h"
#include "../sio.h"

struct suart
{
    enum sio_dev dev;
    struct lpc io;
    uint8_t sirq;
    uint16_t base;
    uint16_t baud;
};

/* If base is 0 the hardware default is selected */
int suart_init_defaults(struct suart *ctx, enum sio_dev dev);
int suart_init(struct suart *ctx, enum sio_dev dev, uint16_t base, int sirq);
int suart_destroy(struct suart *ctx);
int suart_set_baud(struct suart *ctx, int rate);

/* Non-blocking */
ssize_t suart_write(struct suart *ctx, const char *buf, size_t len);
ssize_t suart_read(struct suart *ctx, char *buf, size_t len);

int suart_run(struct suart *ctx, int uin, int uout);

/* Blocking */
ssize_t suart_flush(struct suart *ctx, const char *buf, size_t len);
ssize_t suart_fill(struct suart *ctx, char *buf, size_t len);
ssize_t suart_fill_until(struct suart *ctx, char *buf, size_t len, char term);

#endif
