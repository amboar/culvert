/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _VUART_H
#define _VUART_H

#include "soc.h"

enum vuart_discard { discard_enable, discard_disable };

struct vuart;

int vuart_set_host_tx_discard(struct vuart *ctx, enum vuart_discard state);

struct vuart *vuart_get_by_name(struct soc *soc, const char *name);

#endif
