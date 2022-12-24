/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _SOC_STRAP_H
#define _SOC_STRAP_H

#include "soc.h"

#include <stdint.h>

#define AST2400_SCU_HW_STRAP1   0x070
#define AST2400_SCU_HW_STRAP2   0x0d0
#define AST2500_SCU_HW_STRAP    0x070
#define AST2500_SCU_SILICON_ID  0x07c

struct strap;

int strap_read(struct strap *ctx, int reg, uint32_t *val);
int strap_set(struct strap *ctx, int reg, uint32_t update, uint32_t mask);
int strap_clear(struct strap *ctx, int reg, uint32_t update, uint32_t mask);

struct strap *strap_get(struct soc *soc);

#endif

