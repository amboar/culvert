/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _CLK_H
#define _CLK_H

#include "soc.h"

#include <stdint.h>

enum clksrc { clk_arm, clk_ahb, clk_uart3 };

struct clk;

int64_t clk_get_rate(struct clk *ctx, enum clksrc src);
int clk_enable(struct clk *ctx, enum clksrc src);
int clk_disable(struct clk *ctx, enum clksrc src);

struct clk *clk_get(struct soc *soc);

#endif
