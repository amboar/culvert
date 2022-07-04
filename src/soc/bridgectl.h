/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 IBM Corp. */

#ifndef _SOC_BRIDGECTL_H
#define _SOC_BRIDGECTL_H

#include "ccan/list/list.h"

enum bridge_mode { bm_permissive, bm_restricted, bm_disabled };

struct bridgectl;

struct bridgectl_ops {
    const char *(*name)(struct bridgectl *ctx);
    int (*enforce)(struct bridgectl *ctx, enum bridge_mode mode);
    int (*status)(struct bridgectl *ctx, enum bridge_mode *mode);
    int (*report)(struct bridgectl *ctx, int fd, enum bridge_mode *mode);
};

struct bridgectl {
    struct list_node entry;
    const struct bridgectl_ops *ops;
};

static inline const char *bridgectl_name(struct bridgectl *ctx)
{
    return ctx->ops->name(ctx);
}

static inline int bridgectl_enforce(struct bridgectl *ctx, enum bridge_mode mode)
{
    return ctx->ops->enforce(ctx, mode);
}

static inline int bridgectl_status(struct bridgectl *ctx, enum bridge_mode *mode)
{
    return ctx->ops->status(ctx, mode);
}

static inline int bridgectl_report(struct bridgectl *ctx, int fd, enum bridge_mode *mode)
{
    return ctx->ops->report(ctx, fd, mode);
}

void bridgectl_log_status(struct bridgectl *ctx, int fd, enum bridge_mode mode);
#endif
