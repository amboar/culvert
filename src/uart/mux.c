// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "../ast.h"
#include "../mb.h"

#include "mux.h"

#include <assert.h>
#include <errno.h>
#include <strings.h>
#include <sys/types.h>

#define LPC_HICR9			0x98
#define   LPC_HICR9_SEL6IO		(0b1111 << 8)
#define LPC_HICRA			0x9c
#define   LPC_HICRA_SEL5DW		(0b1111 << 28)
#define   LPC_HICRA_SEL4DW		(0b111 << 25)
#define   LPC_HICRA_SEL3DW		(0b111 << 22)
#define   LPC_HICRA_SEL2DW		(0b111 << 19)
#define   LPC_HICRA_SEL1DW		(0b111 << 16)
#define   LPC_HICRA_SEL5IO		(0b111 << 12)
#define   LPC_HICRA_SEL4IO		(0b111 << 9)
#define   LPC_HICRA_SEL3IO		(0b111 << 6)
#define   LPC_HICRA_SEL2IO		(0b111 << 3)
#define   LPC_HICRA_SEL1IO		(0b111 << 0)

const struct mux_obj _mux_obj_io1 = { .type = mux_io, .io = io1 },
                     *mux_obj_io1 = &_mux_obj_io1;
const struct mux_obj _mux_obj_io3 = { .type = mux_io, .io = io3 },
                     *mux_obj_io3 = &_mux_obj_io3;
const struct mux_obj _mux_obj_uart1 = { .type = mux_uart, .uart = uart1 },
                     *mux_obj_uart1 = &_mux_obj_uart1;
const struct mux_obj _mux_obj_uart2 = { .type = mux_uart, .uart = uart2 },
                     *mux_obj_uart2 = &_mux_obj_uart2;
const struct mux_obj _mux_obj_uart3 = { .type = mux_uart, .uart = uart3 },
                     *mux_obj_uart3 = &_mux_obj_uart3;
const struct mux_obj _mux_obj_uart5 = { .type = mux_uart, .uart = uart5 },
                     *mux_obj_uart5 = &_mux_obj_uart5;

struct uart_mux {
    struct soc *soc;
    struct soc_region lpc;

    uint32_t hicr9;
    uint32_t hicra;
};

struct mux_desc {
    uint32_t reg;
    uint32_t mask;
    uint8_t val;
};

/* mux_io_count > mux_uart_count */
struct mux_lookup {
    struct mux_desc lookup[mux_io_count][mux_io_count];
};

static const struct mux_lookup mux_lookup[2][2] = {
    [mux_uart] = {
        [mux_uart] = {
            .lookup = {
                [uart1] = {
                    [uart2] = { LPC_HICRA, LPC_HICRA_SEL2DW, 0b110 },
                    [uart3] = { LPC_HICRA, LPC_HICRA_SEL3DW, 0b101 },
                    [uart4] = { LPC_HICRA, LPC_HICRA_SEL4DW, 0b100 },
                    [uart5] = { LPC_HICRA, LPC_HICRA_SEL5DW, 0b0101 },
                },
                [uart2] = {
                    [uart1] = { LPC_HICRA, LPC_HICRA_SEL1DW, 0b100 },
                    [uart3] = { LPC_HICRA, LPC_HICRA_SEL3DW, 0b110 },
                    [uart4] = { LPC_HICRA, LPC_HICRA_SEL4DW, 0b101 },
                    [uart5] = { LPC_HICRA, LPC_HICRA_SEL5DW, 0b0110 },
                },
                [uart3] = {
                    [uart1] = { LPC_HICRA, LPC_HICRA_SEL1DW, 0b101 },
                    [uart2] = { LPC_HICRA, LPC_HICRA_SEL2DW, 0b100 },
                    [uart4] = { LPC_HICRA, LPC_HICRA_SEL4DW, 0b110 },
                    [uart5] = { LPC_HICRA, LPC_HICRA_SEL5DW, 0b0111 },
                },
                [uart4] = {
                    [uart1] = { LPC_HICRA, LPC_HICRA_SEL1DW, 0b110 },
                    [uart2] = { LPC_HICRA, LPC_HICRA_SEL2DW, 0b101 },
                    [uart3] = { LPC_HICRA, LPC_HICRA_SEL3DW, 0b100 },
                    [uart5] = { LPC_HICRA, LPC_HICRA_SEL5DW, 0b0111 },
                },
                [uart5] = { }
            },
        },
        [mux_io] = {
            .lookup = {
                [uart1] = {
                    [io1] = { LPC_HICRA, LPC_HICRA_SEL1IO, 0b000 },
                    [io2] = { LPC_HICRA, LPC_HICRA_SEL2IO, 0b100 },
                    [io3] = { LPC_HICRA, LPC_HICRA_SEL3IO, 0b011 },
                    [io4] = { LPC_HICRA, LPC_HICRA_SEL4IO, 0b010 },
                    [io5] = { LPC_HICRA, LPC_HICRA_SEL5IO, 0b001 },
                    [io6] = { LPC_HICR9, LPC_HICR9_SEL6IO, 0b0000, },
                },
                [uart2] = {
                    [io1] = { LPC_HICRA, LPC_HICRA_SEL1IO, 0b001 },
                    [io2] = { LPC_HICRA, LPC_HICRA_SEL2IO, 0b000 },
                    [io3] = { LPC_HICRA, LPC_HICRA_SEL3IO, 0b100 },
                    [io4] = { LPC_HICRA, LPC_HICRA_SEL4IO, 0b011 },
                    [io5] = { LPC_HICRA, LPC_HICRA_SEL5IO, 0b010 },
                    [io6] = { LPC_HICR9, LPC_HICR9_SEL6IO, 0b0001, },
                },
                [uart3] = {
                    [io1] = { LPC_HICRA, LPC_HICRA_SEL1IO, 0b010 },
                    [io2] = { LPC_HICRA, LPC_HICRA_SEL2IO, 0b001 },
                    [io3] = { LPC_HICRA, LPC_HICRA_SEL3IO, 0b000 },
                    [io4] = { LPC_HICRA, LPC_HICRA_SEL4IO, 0b100 },
                    [io5] = { LPC_HICRA, LPC_HICRA_SEL5IO, 0b011 },
                    [io6] = { LPC_HICR9, LPC_HICR9_SEL6IO, 0b0010, },
                },
                [uart4] = {
                    [io1] = { LPC_HICRA, LPC_HICRA_SEL1IO, 0b011 },
                    [io2] = { LPC_HICRA, LPC_HICRA_SEL2IO, 0b010 },
                    [io3] = { LPC_HICRA, LPC_HICRA_SEL3IO, 0b001 },
                    [io4] = { LPC_HICRA, LPC_HICRA_SEL4IO, 0b000 },
                    [io5] = { LPC_HICRA, LPC_HICRA_SEL5IO, 0b100 },
                    [io6] = { LPC_HICR9, LPC_HICR9_SEL6IO, 0b0011, },
                },
                [uart5] = {
                    [io1] = { LPC_HICRA, LPC_HICRA_SEL1IO, 0b100 },
                    [io2] = { LPC_HICRA, LPC_HICRA_SEL2IO, 0b011 },
                    [io3] = { LPC_HICRA, LPC_HICRA_SEL3IO, 0b010 },
                    [io4] = { LPC_HICRA, LPC_HICRA_SEL4IO, 0b001 },
                    [io5] = { LPC_HICRA, LPC_HICRA_SEL5IO, 0b000 },
                    [io6] = { LPC_HICR9, LPC_HICR9_SEL6IO, 0b0100, },
                },
            },
        },
    },
    [mux_io] = {
        [mux_uart] = {
            .lookup = {
                [io1] = {
                    [uart1] = { LPC_HICRA, LPC_HICRA_SEL1DW, 0b000 },
                    [uart2] = { LPC_HICRA, LPC_HICRA_SEL2DW, 0b011 },
                    [uart3] = { LPC_HICRA, LPC_HICRA_SEL3DW, 0b010 },
                    [uart4] = { LPC_HICRA, LPC_HICRA_SEL4DW, 0b001 },
                    [uart5] = { LPC_HICRA, LPC_HICRA_SEL5DW, 0b0001 },
                },
                [io2] = {
                    [uart1] = { LPC_HICRA, LPC_HICRA_SEL1DW, 0b001 },
                    [uart2] = { LPC_HICRA, LPC_HICRA_SEL2DW, 0b000 },
                    [uart3] = { LPC_HICRA, LPC_HICRA_SEL3DW, 0b011 },
                    [uart4] = { LPC_HICRA, LPC_HICRA_SEL4DW, 0b010 },
                    [uart5] = { LPC_HICRA, LPC_HICRA_SEL5DW, 0b0010 },
                },
                [io3] = {
                    [uart1] = { LPC_HICRA, LPC_HICRA_SEL1DW, 0b010 },
                    [uart2] = { LPC_HICRA, LPC_HICRA_SEL2DW, 0b001 },
                    [uart3] = { LPC_HICRA, LPC_HICRA_SEL3DW, 0b000 },
                    [uart4] = { LPC_HICRA, LPC_HICRA_SEL4DW, 0b011 },
                    [uart5] = { LPC_HICRA, LPC_HICRA_SEL5DW, 0b0011 },
                },
                [io4] = {
                    [uart1] = { LPC_HICRA, LPC_HICRA_SEL1DW, 0b011 },
                    [uart2] = { LPC_HICRA, LPC_HICRA_SEL2DW, 0b010 },
                    [uart3] = { LPC_HICRA, LPC_HICRA_SEL3DW, 0b001 },
                    [uart4] = { LPC_HICRA, LPC_HICRA_SEL4DW, 0b000 },
                    [uart5] = { LPC_HICRA, LPC_HICRA_SEL5DW, 0b0100 },
                },
                [io5] = {
                    [uart5] = { LPC_HICRA, LPC_HICRA_SEL5DW, 0b0000 },
                },
                [io6] = {
                    [uart1] = { LPC_HICRA, LPC_HICRA_SEL1DW, 0b111 },
                    [uart2] = { LPC_HICRA, LPC_HICRA_SEL2DW, 0b111 },
                    [uart3] = { LPC_HICRA, LPC_HICRA_SEL3DW, 0b111 },
                    [uart4] = { LPC_HICRA, LPC_HICRA_SEL4DW, 0b111 },
                    [uart5] = { LPC_HICRA, LPC_HICRA_SEL5DW, 0b1001 },
                },
            },
        },
        [mux_io] = {
            .lookup = {
                [io1] = {
                    [io3] = { LPC_HICRA, LPC_HICRA_SEL3IO, 0b101 },
                    [io4] = { LPC_HICRA, LPC_HICRA_SEL4IO, 0b101 },
                    [io5] = { LPC_HICRA, LPC_HICRA_SEL5IO, 0b101 },
                    [io6] = { LPC_HICR9, LPC_HICR9_SEL6IO, 0b0101, },
                },
                [io2] = {
                    [io3] = { LPC_HICRA, LPC_HICRA_SEL3IO, 0b110 },
                    [io4] = { LPC_HICRA, LPC_HICRA_SEL4IO, 0b110 },
                    [io6] = { LPC_HICR9, LPC_HICR9_SEL6IO, 0b0110, },
                },
                [io3] = {
                    [io1] = { LPC_HICRA, LPC_HICRA_SEL1IO, 0b101 },
                    [io2] = { LPC_HICRA, LPC_HICRA_SEL2IO, 0b101 },
                    [io5] = { LPC_HICRA, LPC_HICRA_SEL5IO, 0b110 },
                    [io6] = { LPC_HICR9, LPC_HICR9_SEL6IO, 0b0111, },
                },
                [io4] = {
                    [io1] = { LPC_HICRA, LPC_HICRA_SEL1IO, 0b110 },
                    [io2] = { LPC_HICRA, LPC_HICRA_SEL2IO, 0b110 },
                    [io6] = { LPC_HICR9, LPC_HICR9_SEL6IO, 0b1000, },
                },
                [io5] = {
                    [io6] = { LPC_HICR9, LPC_HICR9_SEL6IO, 0b1001, },
                },
                [io6] = {
                    [io1] = { LPC_HICRA, LPC_HICRA_SEL1IO, 0b111 },
                    [io2] = { LPC_HICRA, LPC_HICRA_SEL2IO, 0b111 },
                    [io3] = { LPC_HICRA, LPC_HICRA_SEL3IO, 0b111 },
                    [io4] = { LPC_HICRA, LPC_HICRA_SEL4IO, 0b111 },
                    [io5] = { LPC_HICRA, LPC_HICRA_SEL5IO, 0b111 },
                },
            },
        },
    },
};

static int lpc_readl(struct uart_mux *ctx, uint32_t offset, uint32_t *val)
{
    return soc_readl(ctx->soc, ctx->lpc.start + offset, val);
}

static int lpc_writel(struct uart_mux *ctx, uint32_t offset, uint32_t val)
{
    return soc_writel(ctx->soc, ctx->lpc.start + offset, val);
}

int uart_mux_restore(struct uart_mux *ctx)
{
    int rc;

    if ((rc = lpc_writel(ctx, LPC_HICR9, ctx->hicr9)) < 0)
        return rc;

    if ((rc = lpc_writel(ctx, LPC_HICRA, ctx->hicra)) < 0)
        return rc;

    return 0;
}

int uart_mux_route(struct uart_mux *ctx, const struct mux_obj *s,
                   const struct mux_obj *d)
{
    const struct mux_desc *md;
    uint32_t val;
    off_t si, di;
    int rc;

    si = s->type == mux_uart ? s->uart : s->io;
    di = d->type == mux_uart ? d->uart : d->io;
    md = &mux_lookup[s->type][d->type].lookup[si][di];
    if (!md->mask)
        return -EINVAL;

    if ((rc = lpc_readl(ctx, md->reg, &val)) < 0)
        return rc;

    val &= ~(md->mask);
    val |= md->val << (ffs(md->mask) - 1);

    if ((rc = lpc_writel(ctx, md->reg, val)) < 0)
        return rc;

    return 0;
}

int uart_mux_connect(struct uart_mux *ctx, const struct mux_obj *a,
                     const struct mux_obj *b)
{
    int rc;

    if ((rc = uart_mux_route(ctx, a, b)) < 0)
        return rc;

    if ((rc = uart_mux_route(ctx, b, a)) < 0)
        return rc;

    return 0;
}

static const struct soc_device_id lpc_match[] = {
    { .compatible = "aspeed,ast2500-lpc-v2" },
    { },
};

static int uart_mux_driver_init(struct soc *soc, struct soc_device *dev)
{
    struct uart_mux *ctx;
    uint32_t val;
    int rc;

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return -ENOMEM;
    }

    if ((rc = soc_device_get_memory(soc, &dev->node, &ctx->lpc)) < 0) {
        goto cleanup_ctx;
    }

    ctx->soc = soc;

    if ((rc = lpc_readl(ctx, LPC_HICR9, &val)) < 0) {
        goto cleanup_ctx;
    }

    ctx->hicr9 = val;

    if ((rc = lpc_readl(ctx, LPC_HICRA, &val)) < 0) {
        return rc;
    }

    ctx->hicra = val;

    soc_device_set_drvdata(dev, ctx);

    return 0;

cleanup_ctx:
    free(ctx);

    return rc;
}

static void uart_mux_driver_destroy(struct soc_device *dev)
{
    free(soc_device_get_drvdata(dev));
}

static const struct soc_driver uart_mux_driver = {
    .name = "uart-mux",
    .matches = lpc_match,
    .init = uart_mux_driver_init,
    .destroy = uart_mux_driver_destroy,
};
REGISTER_SOC_DRIVER(&uart_mux_driver);

struct uart_mux *uart_mux_get(struct soc *soc)
{
    return soc_driver_get_drvdata(soc, &uart_mux_driver);
}
