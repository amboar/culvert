/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _WDT_H
#define _WDT_H

#include "clk.h"
#include "soc.h"

struct wdt;

int wdt_perform_reset(struct wdt *ctx);
int wdt_prevent_reset(struct soc *soc);

struct wdt *wdt_get_by_name(struct soc *soc, const char *name);

#endif
