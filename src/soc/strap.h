/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _SOC_STRAP_H
#define _SOC_STRAP_H

#include "soc.h"

#include <stdint.h>

struct strap;

int strap_read(struct strap *ctx, int reg, uint32_t *val);
int strap_set(struct strap *ctx, int reg, uint32_t update, uint32_t mask);
int strap_clear(struct strap *ctx, int reg, uint32_t update, uint32_t mask);

struct strap *strap_get(struct soc *soc);

#endif

