/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _HOST_H
#define _HOST_H

#include "ahb.h"
#include "connection.h"

#include "ccan/list/list.h"

struct host {
	struct list_head bridges;
};

int host_init(struct host *ctx, struct connection_args *connection);
void host_destroy(struct host *ctx);

int disable_bridge_driver(const char *drv);
void print_bridge_drivers(void);

/**
 * get_bridge_driver - Look up and return a matching bridge driver
 *
 * Searches the autodata section for a bridge driver matching the given name
 * and not marked as disabled. If found, sets the provided output pointer to
 * the matching driver.
 *
 * @param drv:     Name of the bridge driver to look for (must not be NULL).
 * @param bridge:  Output pointer that will be set to the matching bridge
 *                 driver, if one is found. Must not be NULL.
 *
 * @return 0 if a matching driver is found and returned,
 *         -ENOENT if no match was found.
 *
 * Note: The returned driver pointer is owned by the autodata system.
 *       Do not free or modify it.
 */
int get_bridge_driver(const char *drv, struct bridge_driver **bridge);

struct ahb *host_get_ahb(struct host *ctx);

static inline int host_bridge_release_from_ahb(struct ahb *ahb)
{
	return ahb->drv->release ? ahb->drv->release(ahb) : 0;
}

static inline int host_bridge_reinit_from_ahb(struct ahb *ahb)
{
	return ahb->drv->reinit ? ahb->drv->reinit(ahb) : 0;
}

#endif
