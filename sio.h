/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef SIO_H
#define SIO_H

#include <stddef.h>
#include <stdint.h>

#include "lpc.h"

enum sio_dev
{
    sio_suart1 = 0x02,
    sio_suart2 = 0x03,
    sio_wakeup = 0x04,
    sio_gpio   = 0x07,
    sio_suart3 = 0x0b,
    sio_suart4 = 0x0c,
    sio_ilpc   = 0x0d,
    sio_mbox   = 0x0e,
};

struct sio
{
    struct lpc io;
};

int sio_init(struct sio *ctx);
int sio_destroy(struct sio *ctx);
int sio_lock(struct sio *ctx);
int sio_unlock(struct sio *ctx);
int sio_select(struct sio *ctx, enum sio_dev dev);
int sio_present(struct sio *ctx);
int sio_readb(struct sio *ctx, uint32_t addr, uint8_t *val);
int sio_writeb(struct sio *ctx, uint32_t addr, uint8_t val);

#endif
