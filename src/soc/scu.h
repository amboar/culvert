// SPDX-License-Identifier: Apache-2.0

#ifndef _SOC_SCU_H
#define _SOC_SCU_H

#include "soc.h"

struct scu;

struct scu *scu_get(struct soc *soc);
void scu_put(struct scu *ctx);

int scu_readl(struct scu *ctx, uint32_t reg, uint32_t *val);
int scu_writel(struct scu *ctx, uint32_t reg, uint32_t val);

#endif
