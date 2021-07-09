/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _VUART_H
#define _VUART_H

#include "soc.h"

enum vuart_discard { discard_enable, discard_disable };

struct vuart {
	struct soc *soc;
	struct soc_region iomem;
};

int vuart_init(struct vuart *ctx, struct soc *soc, const char *path);
int vuart_set_host_tx_discard(struct vuart *ctx, enum vuart_discard state);
void vuart_destroy(struct vuart *ctx);

#endif
