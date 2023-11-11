// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2022 IBM Corp.

#include "ahb.h"
#include "bridge.h"
#include "bridge/debug.h"
#include "bridge/devmem.h"
#include "bridge/ilpc.h"
#include "bridge/l2a.h"
#include "bridge/p2a.h"
#include "compiler.h"
#include "host.h"
#include "log.h"

#include <errno.h>

struct bridge {
	struct list_node entry;
	const struct bridge_driver *driver;
	struct ahb *ahb;
};

int host_init(struct host *ctx, int argc, char *argv[])
{
    struct bridge_driver **bridges;
    size_t n_bridges = 0;
    size_t i;
    int rc;

    list_head_init(&ctx->bridges);

    bridges = autodata_get(bridge_drivers, &n_bridges);

    logd("Found %zu registered bridge drivers\n", n_bridges);

    for (i = 0; i < n_bridges; i++) {
        struct ahb *ahb;

        logd("Trying bridge driver %s\n", bridges[i]->name);

        if ((ahb = bridges[i]->probe(argc, argv))) {
            struct bridge *bridge;

            bridge = malloc(sizeof(*bridge));
            if (!bridge) {
                rc = -ENOMEM;
                goto cleanup_bridges;
            }

            bridge->driver = bridges[i];
            bridge->ahb = ahb;

            list_add(&ctx->bridges, &bridge->entry);
        }
    }

    rc = 0;

cleanup_bridges:
    autodata_free(bridges);

    return rc;
}

void host_destroy(struct host *ctx)
{
    struct bridge *bridge, *next;

    list_for_each_safe(&ctx->bridges, bridge, next, entry) {
        bridge->driver->destroy(bridge->ahb);
        list_del(&bridge->entry);
        free(bridge);
    }
}

struct ahb *host_get_ahb(struct host *ctx)
{
    struct bridge *bridge;

    bridge = list_top(&ctx->bridges, struct bridge, entry);

    return bridge ? bridge->ahb : NULL;
}

int host_bridge_reinit_from_ahb(struct host *ctx, struct ahb *ahb)
{
    struct bridge *bridge, *next;

    list_for_each_safe(&ctx->bridges, bridge, next, entry) {
        if (bridge->ahb != ahb) {
            continue;
        }

        return bridge->driver->reinit ? bridge->driver->reinit(bridge->ahb) : 0;
    }

    return 0;
}
