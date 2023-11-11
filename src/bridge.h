/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _BRIDGE_H
#define _BRIDGE_H

#include <stdbool.h>

#include "ahb.h"

#include "ccan/autodata/autodata.h"

struct bridge_driver {
	const char *name;
	struct ahb *(*probe)(int argc, char *argv[]);
	int (*reinit)(struct ahb *ahb);
	void (*destroy)(struct ahb *ahb);

	/*
	 * Whether or not this driver is for running culvert on the BMC itself
	 * (i.e. devmem)
	 */
	bool local;
};

AUTODATA_TYPE(bridge_drivers, struct bridge_driver);
#define REGISTER_BRIDGE_DRIVER(bd) AUTODATA_SYM(bridge_drivers, bd)

#endif
