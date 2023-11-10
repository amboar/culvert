/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _HOST_H
#define _HOST_H

#include "ahb.h"

#include "ccan/list/list.h"

struct host {
	struct list_head bridges;
};

int on_each_bridge_driver(int (*fn)(struct bridge_driver*, void*), void *arg);

int host_init(struct host *ctx, int argc, char *argv[]);
void host_destroy(struct host *ctx);

struct ahb *host_get_ahb(struct host *ctx);
int host_bridge_reinit_from_ahb(struct host *ctx, struct ahb *ahb);

#endif
