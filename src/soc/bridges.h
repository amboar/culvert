/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 IBM Corp. */

#ifndef _SOC_BRIDGES_H
#define _SOC_BRIDGES_H

#include "soc.h"

struct bridges;

struct bridges *bridges_get(struct soc *soc);

int bridges_device_get_gates(struct soc *soc, struct soc_device *dev, struct bridges **bridges,
                             int *gate);

int bridges_enable(struct bridges *ctx, int bridge);
int bridges_disable(struct bridges *ctx, int bridge);
int bridges_status(struct bridges *ctx, int bridge);

#endif
