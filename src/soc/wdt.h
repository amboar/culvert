/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _WDT_H
#define _WDT_H

#include "clk.h"
#include "soc.h"

int wdt_prevent_reset(struct soc *soc);

struct wdt;

int wdt_init(struct wdt *ctx, struct soc *soc, const char *name);
int wdt_perform_reset(struct wdt *ctx);
void wdt_destroy(struct wdt *ctx);

struct wdt *wdt_get_by_name(struct soc *soc, const char *name);

#endif
