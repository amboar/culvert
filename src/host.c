// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2022 IBM Corp.

#include "ahb.h"
#include "bridge.h"
#include "bridge/debug.h"
#include "bridge/devmem.h"
#include "bridge/ilpc.h"
#include "bridge/l2a.h"
#include "bridge/p2a.h"
#include "connection.h"
#include "compiler.h"
#include "host.h"
#include "lock.h"
#include "log.h"

#include "ccan/autodata/autodata.h"

#include <errno.h>

struct bridge {
	struct list_node entry;
	const struct bridge_driver *driver;
	struct ahb *ahb;
	int lock;
};

void print_bridge_drivers(void)
{
	struct bridge_driver **bridges;
	size_t n_bridges = 0;

	printf("Available bridges:\n");

	bridges = autodata_get(bridge_drivers, &n_bridges);

	for (size_t i = 0; i < n_bridges; i++) {
		printf("  %s\n", bridges[i]->name);
	}

	autodata_free(bridges);
}

int disable_bridge_driver(const char *drv)
{
	struct bridge_driver **bridges;
	size_t n_bridges = 0;
	int ret = -ENOENT;

	bridges = autodata_get(bridge_drivers, &n_bridges);

	for (size_t i = 0; i < n_bridges; i++) {
		if (!strcmp(bridges[i]->name, drv)) {
			bridges[i]->disabled = true;
			ret = 0;
			goto out;
		}
	}

out:
	autodata_free(bridges);

	return ret;
}

int get_bridge_driver(const char *drv, struct bridge_driver **bridge)
{
	struct bridge_driver **bridges;
	size_t n_bridges = 0;
	int ret = -ENOENT;

	bridges = autodata_get(bridge_drivers, &n_bridges);

	for (size_t i = 0; i < n_bridges; i++) {
		if (!strcmp(bridges[i]->name, drv) && !bridges[i]->disabled) {
			*bridge = bridges[i];
			ret = 0;
			goto out;
		}
	}

out:
	autodata_free(bridges);

	return ret;
}

static inline int host_probe_bridge(struct host *ctx,
				    struct bridge_driver *driver,
				    struct connection_args *connection)
{
	struct ahb *ahb;
	struct bridge *bridge;
	int lock;

	if (driver->disabled) {
		logd("Skipping bridge driver %s\n", driver->name);
		return 0;
	}

	logd("Trying bridge driver %s\n", driver->name);

	/*
	 * Hold the bridge's lock across the probe attempt as well as its
	 * subsequent use, not just the successful case: probing can itself
	 * drive the bridge hardware (e.g. to fingerprint what's present),
	 * which is exactly the kind of access that must not race against
	 * another culvert instance.
	 */
	lock = lock_acquire(driver->name);
	if (lock < 0)
		return lock;

	ahb = driver->probe(connection);
	if (!ahb) {
		lock_release(lock);
		return 0;
	}

	bridge = malloc(sizeof(*bridge));
	if (!bridge) {
		lock_release(lock);
		return -ENOMEM;
	}

	bridge->driver = driver;
	bridge->ahb = ahb;
	bridge->lock = lock;

	list_add(&ctx->bridges, &bridge->entry);
	return 0;
}

int host_init(struct host *ctx, struct connection_args *connection)
{
	struct bridge_driver **bridges;
	size_t n_bridges;
	int rc;

	/* Always init head for legacy reasons */
	list_head_init(&ctx->bridges);

	/* If a bridge driver is defined, use it instead of probing all */
	if (connection->bridge_driver != NULL) {
		logd("Host probing found bridge driver '%s', using it\n",
		     connection->bridge_driver->name);
		rc = host_probe_bridge(ctx, connection->bridge_driver,
				       connection);
		goto done;
	}

	bridges = autodata_get(bridge_drivers, &n_bridges);
	logd("Found %zu registered bridge drivers\n", n_bridges);

	for (size_t i = 0; i < n_bridges; i++) {
		rc = host_probe_bridge(ctx, bridges[i], connection);
		if (rc < 0)
			goto cleanup_bridges;
	}

cleanup_bridges:
	autodata_free(bridges);

done:
	return rc;
}

void host_destroy(struct host *ctx)
{
	struct bridge *bridge, *next;

	list_for_each_safe(&ctx->bridges, bridge, next, entry) {
		bridge->driver->destroy(bridge->ahb);
		lock_release(bridge->lock);
		list_del(&bridge->entry);
		free(bridge);
	}
}

struct ahb *host_get_ahb(struct host *ctx)
{
	struct bridge *bridge;

	bridge = list_top(&ctx->bridges, struct bridge, entry);

	if (bridge) {
		logd("Accessing the BMC's AHB via the %s bridge\n",
		     bridge->driver->name);
		return bridge->ahb;
	}

	loge("Bridge discovery failed, cannot access BMC AHB\n");
	return NULL;
}
