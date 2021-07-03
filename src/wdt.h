/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _WDT_H
#define _WDT_H

#include "ahb.h"

int wdt_prevent_reset(struct ahb *ahb);
int64_t wdt_perform_reset(struct ahb *ahb);

#endif
