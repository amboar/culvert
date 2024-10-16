// SPDX-License-Identifier: Apache-2.0

#ifndef _SOC_JTAG_H
#define _SOC_JTAG_H

#include "soc.h"
#include "bits.h"

#define   SCU_JTAG_NORMAL               0
#define   SCU_JTAG_IO_TO_PCIE           BIT(14)
#define   SCU_JTAG_MASTER_TO_PCIE       BIT(15)
#define   SCU_JTAG_MASTER_TO_ARM        BIT(15) | BIT(14)

struct jtag;

struct jtag *jtag_get(struct soc *soc, const char *name);
void jtag_put(struct jtag *ctx);
int jtag_bitbang_set(struct jtag *ctx, uint8_t tck, uint8_t tms, uint8_t tdi);
int jtag_bitbang_get(struct jtag *ctx, uint8_t* tdo);
int jtag_route(struct jtag *ctx, uint32_t route);

#endif
