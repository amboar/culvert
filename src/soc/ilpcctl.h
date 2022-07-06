/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 IBM Corp. */

#include "soc.h"

struct ilpcctl;

struct ilpcctl *ilpcctl_get(struct soc* soc);
struct bridgectl *ilpcctl_as_bridgectl(struct ilpcctl *ctx);
