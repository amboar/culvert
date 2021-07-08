// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "compiler.h"

#include "clk.h"
#include "soc.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>

#define SCU_HW_STRAP			0x70
#define   SCU_HW_STRAP_UART_DBG_SEL	(1 << 29)
#define   SCU_HW_STRAP_SIO_DEC          (1 << 20)
#define   SCU_HW_STRAP_ARM_CLK          (1 <<  0)
#define SCU_SILICON_REVISION		0x7c

static const struct soc_device_id scu_match[] = {
    { .compatible = "aspeed,ast2500-scu" },
    { },
};

int clk_init(struct clk *ctx, struct soc *soc)
{
    struct soc_device_node dn;
    int rc;

    if ((rc = soc_device_match_node(soc, scu_match, &dn)) < 0)
        return rc;

    if ((rc = soc_device_get_memory(soc, &dn, &ctx->scu)) < 0)
        return rc;

    ctx->soc = soc;

    return 0;
}

void clk_destroy(struct clk *ctx)
{
    ctx->soc = NULL;
}

static int clk_readl(struct clk *ctx, uint32_t offset, uint32_t *val)
{
    return soc_readl(ctx->soc, ctx->scu.start + offset, val);
}

static int64_t clk_rate_ahb(struct clk *ctx)
{
    static const uint32_t cpu_freqs_24_48[] = {
	384000000,
	360000000,
	336000000,
	408000000
    };
    static const uint32_t cpu_freqs_25[] = {
	400000000,
	375000000,
	350000000,
	425000000
    };
    static const uint32_t ahb_div[] = { 1, 2, 4, 3 };
    uint32_t strap, cpu_clk, div;
    int rc;

    /* HW strapping gives us the CPU freq and AHB divisor */
    if ((rc = clk_readl(ctx, SCU_HW_STRAP, &strap)) < 0)
        return rc;

    if (strap & 0x00800000)
	cpu_clk = cpu_freqs_25[(strap >> 8) & 3];
    else
	cpu_clk = cpu_freqs_24_48[(strap >> 8) & 3];

    div = ahb_div[(strap >> 10) & 3];

    return cpu_clk / div;
}

int64_t clk_get_rate(struct clk *ctx, enum clksrc src)
{
    if (src != clk_ahb)
        return -ENOTSUP;

    return clk_rate_ahb(ctx);
}

int clk_disable(struct clk *ctx, enum clksrc src)
{
    if (src != clk_arm)
        return -ENOTSUP;

    return soc_writel(ctx->soc, ctx->scu.start | SCU_HW_STRAP,
                      SCU_HW_STRAP_ARM_CLK);
}

int clk_enable(struct clk *ctx, enum clksrc src)
{
    if (src != clk_arm)
        return -ENOTSUP;

    return soc_writel(ctx->soc, ctx->scu.start | SCU_SILICON_REVISION,
                      SCU_HW_STRAP_ARM_CLK);
}
