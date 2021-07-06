/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _CLK_H
#define _CLK_H

#include "ahb.h"

enum clk_gate { clk_arm };

int clk_enable(struct ahb *ahb, enum clk_gate gate);
int clk_disable(struct ahb *ahb, enum clk_gate gate);

#endif
