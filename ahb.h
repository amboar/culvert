/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _AHB_H
#define _AHB_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

enum ahb_bridge {
    ahb_ilpcb,
    ahb_l2ab,
    ahb_p2ab,
    ahb_debug,
    ahb_devmem,
    ahb_max_interfaces
};

extern const char *ahb_interface_names[ahb_max_interfaces];

struct ahb_range {
    const char *name;
    uint32_t start;
    uint64_t len;
    bool rw;
};

struct ilpcb;
struct l2ab;
struct p2ab;
struct debug;
struct devmem;

struct ahb {
    enum ahb_bridge bridge;
    union {
        struct ilpcb *ilpcb;
        struct l2ab *l2ab;
        struct p2ab *p2ab;
        struct debug *debug;
        struct devmem *devmem;
    };
};

struct ahb *ahb_use(struct ahb *ctx, enum ahb_bridge type, void *bridge);

int ahb_init(struct ahb *ctx, enum ahb_bridge type, ...);

/* Tear-down the AHB interface when SoC has *not* been reset */
int ahb_destroy(struct ahb *ctx);

/* Tear-down the AHB interface when SoC *has* been reset */
int ahb_cleanup(struct ahb *ctx);

ssize_t ahb_read(struct ahb *ctx, uint32_t phys, void *buf, size_t len);
ssize_t ahb_write(struct ahb *ctx, uint32_t phys, const void *buf, size_t len);

int ahb_readl(struct ahb *ctx, uint32_t phys, uint32_t *val);
int ahb_writel(struct ahb *ctx, uint32_t phys, uint32_t val);

ssize_t ahb_siphon_in(struct ahb *ctx, uint32_t phys, size_t len, int outfd);
ssize_t ahb_siphon_out(struct ahb *ctx, uint32_t phys, int infd);

#endif
