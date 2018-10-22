/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _VUART_H
#define _VUART_H

#include "ahb.h"

#include <stdbool.h>

enum vuart_discard { discard_enable, discard_disable };

int vuart_set_host_tx_discard(struct ahb *ahb, enum vuart_discard state);

#endif
