/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2021 IBM Corp. */
#ifndef _SDMC_H
#define _SDMC_H

#include "soc.h"

#include <stddef.h>

struct sdmc_pdata {
    const uint32_t (*dram_sizes)[4];
    const uint32_t (*vram_sizes)[4];
    const uint32_t gmp_xdma_mask;
};

struct sdmc {
	struct soc *soc;
	struct soc_region iomem;
	struct soc_region dram;
	const struct sdmc_pdata *pdata;
};

int sdmc_init(struct sdmc *ctx, struct soc *soc);
int sdmc_get_dram(struct sdmc *ctx, struct soc_region *dram);
int sdmc_get_vram(struct sdmc *ctx, struct soc_region *vram);
int sdmc_constrains_xdma(struct sdmc *ctx);
void sdmc_destroy(struct sdmc *ctx);

#endif
