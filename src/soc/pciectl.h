/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 IBM Corp. */

#ifndef _SOC_PCIECTL_H
#define _SOC_PCIECTL_H

#include "soc.h"

struct pciectl;
struct p2actl;
struct xdmactl;

enum pcie_device { pcie_device_vga, pcie_device_bmc };

struct pciectl *pciectl_get(struct soc *soc);

struct bridgectl *p2actl_as_bridgectl(struct pciectl *ctx);
struct bridgectl *xdmactl_as_bridgectl(struct pciectl *ctx);

#endif
