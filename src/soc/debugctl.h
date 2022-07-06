/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 IBM Corp. */

#include "soc.h"

struct debugctl;

struct debugctl *debugctl_get(struct soc *soc);
struct bridgectl *debugctl_as_bridgectl(struct debugctl *ctx);
