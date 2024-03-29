/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2021 IBM Corp. */
#ifndef _SDMC_H
#define _SDMC_H

#include "soc.h"

#include <stddef.h>

struct sdmc;

int sdmc_get_dram(struct sdmc *ctx, struct soc_region *dram);
int sdmc_get_vram(struct sdmc *ctx, struct soc_region *vram);
int sdmc_constrains_xdma(struct sdmc *ctx);
int sdmc_configure_xdma(struct sdmc *ctx, bool constrain);

struct sdmc *sdmc_get(struct soc *soc);

#endif
