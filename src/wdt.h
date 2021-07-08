/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _WDT_H
#define _WDT_H

#include "soc.h"

int wdt_prevent_reset(struct soc *soc);

struct wdt {
	struct soc *soc;
	struct soc_region iomem;
};

int wdt_init(struct wdt *ctx, struct soc *soc, const char *name);
int64_t wdt_perform_reset(struct wdt *ctx);
void wdt_destroy(struct wdt *ctx);

#endif
