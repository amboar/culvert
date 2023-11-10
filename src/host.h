/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _HOST_H
#define _HOST_H

#include "ahb.h"

#include "ccan/list/list.h"

struct host {
	struct list_head bridges;
};

int host_init(struct host *ctx, int argc, char *argv[]);
void host_destroy(struct host *ctx);

int disable_bridge_driver(const char *drv);
void print_bridge_drivers(void);

struct ahb *host_get_ahb(struct host *ctx);
static inline int host_bridge_reinit_from_ahb(struct ahb *ahb)
{
	return ahb->drv->reinit ? ahb->drv->reinit(ahb) : 0;
}

#endif
