/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 IBM Corp. */

#ifndef _SOC_BRIDGES_H
#define _SOC_BRIDGES_H

#include "soc.h"

struct bridges;

struct bridges *bridges_get_by_device(struct soc *soc, struct soc_device *dev);

int bridges_device_get_gate(struct soc *soc, struct soc_device *dev, struct bridges **bridges,
                             int *gate);
int bridges_device_get_gate_by_index(struct soc *soc, struct soc_device *dev, int index,
                                     struct bridges **bridges, int *gate);
int bridges_device_get_gate_by_name(struct soc *soc, struct soc_device *dev, const char *name,
                                    struct bridges **bridges, int *gate);

int bridges_enable(struct bridges *ctx, int bridge);
int bridges_disable(struct bridges *ctx, int bridge);
int bridges_status(struct bridges *ctx, int bridge);

#endif
