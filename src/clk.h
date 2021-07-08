/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _CLK_H
#define _CLK_H

#include "soc.h"

#include <stdint.h>

enum clksrc { clk_arm, clk_ahb, clk_uart3 };

struct clk {
	struct soc *soc;
	struct soc_region scu;
};

int clk_init(struct clk *ctx, struct soc *soc);
void clk_destroy(struct clk *ctx);
int64_t clk_get_rate(struct clk *ctx, enum clksrc src);
int clk_enable(struct clk *ctx, enum clksrc src);
int clk_disable(struct clk *ctx, enum clksrc src);

#endif
