/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _UART_MUX_H
#define _UART_MUX_H

#include <stdint.h>

#include "soc.h"

enum mux_type { mux_io, mux_uart, mux_type_count };
enum mux_io { io1, io2, io3, io4, io5, io6, mux_io_count };
enum mux_uart { uart1, uart2, uart3, uart4, uart5, mux_uart_count };

struct mux_obj {
    enum mux_type type;
    union {
        enum mux_io io;
        enum mux_uart uart;
    };
};

extern const struct mux_obj *mux_obj_io1;
extern const struct mux_obj *mux_obj_io3;
extern const struct mux_obj *mux_obj_uart1;
extern const struct mux_obj *mux_obj_uart2;
extern const struct mux_obj *mux_obj_uart3;
extern const struct mux_obj *mux_obj_uart5;

struct uart_mux;

int uart_mux_restore(struct uart_mux *ctx);

/* Uni-directional connection */
int uart_mux_route(struct uart_mux *ctx, const struct mux_obj *s,
                   const struct mux_obj *d);

/* Bi-directional connection */
int uart_mux_connect(struct uart_mux *ctx, const struct mux_obj *a,
                     const struct mux_obj *b);

struct uart_mux *uart_mux_get(struct soc *soc);

#endif
