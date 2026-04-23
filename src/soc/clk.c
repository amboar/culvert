// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "compiler.h"

#include "bits.h"
#include "clk.h"
#include "scu.h"
#include "soc.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

/* Shared registers between the AST2400 and 2500 will not (!) have a prefix */
#define SCU_CLK_STOP			0x0c
#define 	SCU_CLK_STOP_UART3			BIT(25)

#define SCU_HW_STRAP			0x70
#define 	SCU_HW_STRAP_UART_DBG_SEL		BIT(29)
#define 	SCU_HW_STRAP_CLKIN_IN_MOD		BIT(23)
#define 	SCU_HW_STRAP_SIO_DEC			BIT(20)
#define 	SCU_HW_STRAP_AXI_CLK_FREQ_RATIO_MASK	GENMASK(11, 9)
#define 	SCU_HW_STRAP_AXI_CLK_FREQ_RATIO_SHIFT	9
#define 	SCU_HW_STRAP_ARM_CLK			BIT(0)

#define SCU_SILICON_REVISION		0x7c

#define AST2500_SCU_H_PLL		0x24

union ast2500_h_pll_reg {
	uint32_t w;
	struct {
		uint32_t n : 5;		/* bit[4:0]	*/
		uint32_t m : 8;		/* bit[12:5]	*/
		uint32_t p : 6;		/* bit[18:13]	*/
		uint32_t off : 1;	/* bit[19]	*/
		uint32_t bypass : 1;	/* bit[20]	*/
		uint32_t reset : 1;	/* bit[21]	*/
		uint32_t reserved : 10;	/* bit[31:22]	*/
	} b;
};

/* We only care about SCU004 for now, revision is not required here */
#define AST2600_SCU_SILICON_REVISION	0x04

#define AST2600_SCU_H_PLL		0x200

union ast2600_h_pll_reg {
	uint32_t w;
	struct {
		uint32_t m : 13;	/* bit[12:0]	*/
		uint32_t n : 6;		/* bit[18:13]	*/
		uint32_t p : 4;		/* bit[22:19]	*/
		uint32_t off : 1;	/* bit[23]	*/
		uint32_t bypass : 1;	/* bit[24]	*/
		uint32_t reset : 1;	/* bit[25]	*/
		uint32_t reserved : 6;	/* bit[31:26]	*/
	} b;
};

#define AST2600_SCU_HW_STRAP1		0x500
#define 	AST2600_SCU_HW_STRAP1_ARM_CLK			BIT(0)
#define 	AST2600_SCU_HW_STRAP1_CPU_AXI_RATIO		BIT(16)
#define 	AST2600_SCU_HW_STRAP1_AHB_FREQ_RATIO_MASK	GENMASK(12, 11)
#define 	AST2600_SCU_HW_STRAP1_AHB_FREQ_RATIO_SHIFT	11

#define	AST2600_SCU_HW_STRAP1_CLEAR	0x504

struct clk_ops {
	int64_t (*rate_ahb)(struct clk *ctx);
	int (*disable)(struct clk *ctx, enum clksrc src);
	int (*enable)(struct clk *ctx, enum clksrc src);
};

struct clk {
	struct scu *scu;
	const struct clk_ops *ops;
};

int64_t clk_get_rate(struct clk *ctx, enum clksrc src)
{
	if (src != clk_ahb)
		return -ENOTSUP;

	return ctx->ops->rate_ahb(ctx);
}

int clk_disable(struct clk *ctx, enum clksrc src)
{
	return ctx->ops->disable(ctx, src);
}

int clk_enable(struct clk *ctx, enum clksrc src)
{
	return ctx->ops->enable(ctx, src);
}

int64_t ast2400_clk_rate_ahb(struct clk *ctx)
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
	if ((rc = scu_readl(ctx->scu, SCU_HW_STRAP, &strap)) < 0)
		return rc;

	if (strap & SCU_HW_STRAP_CLKIN_IN_MOD)
		cpu_clk = cpu_freqs_25[(strap >> 8) & 3];
	else
		cpu_clk = cpu_freqs_24_48[(strap >> 8) & 3];

	div = ahb_div[(strap >> 10) & 3];

	return cpu_clk / div;
}

int ast2400_clk_disable(struct clk *ctx, enum clksrc src)
{
	uint32_t reg;
	int rc;

	switch (src) {
	case clk_arm:
		return scu_writel(ctx->scu, SCU_HW_STRAP, SCU_HW_STRAP_ARM_CLK);
	case clk_uart3:
		if ((rc = scu_readl(ctx->scu, SCU_CLK_STOP, &reg)) < 0)
			return rc;

		reg |= SCU_CLK_STOP_UART3;

		if ((rc = scu_writel(ctx->scu, SCU_CLK_STOP, reg)) < 0)
			return rc;

		return 0;
	default:
		break;
	}

	return -ENOTSUP;
}

int ast2400_clk_enable(struct clk *ctx, enum clksrc src)
{
	uint32_t reg;
	int rc;

	switch (src) {
	case clk_arm:
		return scu_writel(ctx->scu, SCU_SILICON_REVISION,
					    SCU_HW_STRAP_ARM_CLK);
	case clk_uart3:
		if ((rc = scu_readl(ctx->scu, SCU_CLK_STOP, &reg)) < 0)
			return rc;

		reg &= ~SCU_CLK_STOP_UART3;

		if ((rc = scu_writel(ctx->scu, SCU_CLK_STOP, reg)) < 0)
			return rc;

		return 0;
	default:
		break;
	}

	return -ENOTSUP;
}

static const struct clk_ops ast2400_clk_ops = {
	.rate_ahb = ast2400_clk_rate_ahb,
	.disable = ast2400_clk_disable,
	.enable = ast2400_clk_enable,
};

int64_t ast2500_clk_rate_ahb(struct clk *ctx)
{
	union ast2500_h_pll_reg h_pll_reg;
	uint32_t strap, ahb_ratio, h_pll, hclk;
	bool clk_25mhz;
	int rc;

	/* HW strapping gives us CLKIN and the AHB divisor */
	if ((rc = scu_readl(ctx->scu, SCU_HW_STRAP, &strap)) < 0)
		return rc;

	/* The H-PLL register allows us to calculate the CPU freq */
	if ((rc = scu_readl(ctx->scu, AST2500_SCU_H_PLL, &h_pll_reg.w)) < 0)
		return rc;

	clk_25mhz = strap & SCU_HW_STRAP_CLKIN_IN_MOD;
	logt("clk: ast2500: clk is %d MHz\n", (clk_25mhz ? 25 : 24));

	/*
	 * H-PLL = CLKIN * [(M+1) / (N+1)] / (P+1)
	 * The usual frequency for H-PLL with CLKIN 24 MHz is 792 MHz.
	 */
	h_pll = (clk_25mhz ? 25 : 24)
		* (h_pll_reg.b.m + 1)
		/ (h_pll_reg.b.n + 1)
		/ (h_pll_reg.b.p + 1);
	logt("clk: ast2500: calculated h-pll is %d MHz\n", h_pll);

	/* AXI/AHB clock frequency ratio selection */
	ahb_ratio = (strap & SCU_HW_STRAP_AXI_CLK_FREQ_RATIO_MASK)
			>> SCU_HW_STRAP_AXI_CLK_FREQ_RATIO_SHIFT;
	if (!ahb_ratio)
		return -EINVAL;
	/*
	 * Incrementing the AHB ratio by one returns the actual ratio
	 * (e.g. mask 0b001 represents a 2:1 ratio.)
	 */
	ahb_ratio++;
	logt("clk: ast2500: ahb ratio: %d:1\n", ahb_ratio);

	/* HCLK = H-PLL / 2 / ahb_ratio */
	hclk = h_pll / 2 / ahb_ratio;
	logt("clk: ast2500: ahb freq: %d MHz\n", hclk);

	return hclk;
}

int ast2500_clk_disable(struct clk *ctx, enum clksrc src)
{
	uint32_t reg;
	int rc;

	switch (src) {
	case clk_arm:
		return scu_writel(ctx->scu, SCU_HW_STRAP, SCU_HW_STRAP_ARM_CLK);
	case clk_uart3:
		if ((rc = scu_readl(ctx->scu, SCU_CLK_STOP, &reg)) < 0)
			return rc;

		reg |= SCU_CLK_STOP_UART3;

		if ((rc = scu_writel(ctx->scu, SCU_CLK_STOP, reg)) < 0)
			return rc;

		return 0;
	default:
		break;
	}

	return -ENOTSUP;
}

int ast2500_clk_enable(struct clk *ctx, enum clksrc src)
{
	uint32_t reg;
	int rc;

	switch (src) {
	case clk_arm:
		return scu_writel(ctx->scu, SCU_SILICON_REVISION,
					    SCU_HW_STRAP_ARM_CLK);
	case clk_uart3:
		if ((rc = scu_readl(ctx->scu, SCU_CLK_STOP, &reg)) < 0)
			return rc;

		reg &= ~SCU_CLK_STOP_UART3;

		if ((rc = scu_writel(ctx->scu, SCU_CLK_STOP, reg)) < 0)
			return rc;

		return 0;
	default:
		break;
	}

	return -ENOTSUP;
}

static const struct clk_ops ast2500_clk_ops = {
	.rate_ahb = ast2500_clk_rate_ahb,
	.disable = ast2500_clk_disable,
	.enable = ast2500_clk_enable,
};

static int64_t ast2600_clk_rate_ahb(struct clk *ctx)
{
	union ast2600_h_pll_reg h_pll_reg;
	uint32_t strap, ahb_ratio, cpu_axi_ratio, h_pll, hclk;
	int rc;

	if ((rc = scu_readl(ctx->scu, AST2600_SCU_HW_STRAP1, &strap)) < 0)
		return rc;

	/* The H-PLL register allows us to calculate the CPU freq */
	if ((rc = scu_readl(ctx->scu, AST2600_SCU_H_PLL, &h_pll_reg.w)) < 0)
		return rc;

	/* H-PLL = CLKIN (always 25 MHz) * (M+1/N+1) / P+1 */
	h_pll = 25
		* (h_pll_reg.b.m + 1)
		/ (h_pll_reg.b.n + 1)
		/ (h_pll_reg.b.p + 1);
	logt("clk: ast2600: calculated h-pll is %d MHz\n", h_pll);

	cpu_axi_ratio = (strap & AST2600_SCU_HW_STRAP1_CPU_AXI_RATIO);

	/* AXI/AHB clock frequency ratio selection */
	ahb_ratio = (strap & AST2600_SCU_HW_STRAP1_AHB_FREQ_RATIO_MASK)
			>> AST2600_SCU_HW_STRAP1_AHB_FREQ_RATIO_SHIFT;
	if (!ahb_ratio)
		return -EINVAL;

	/*
	 * If cpu_axi_ratio is set, then the ratios are default, 4, 6, 8.
	 * Otherwise, the ratios are default, 2, 3, 4.
	 * The default is "that makes HCLK = 200MHz", but there's no explanation
	 * in the datasheet how HCLK is being calculated so just return the
	 * reference value of 200 MHz as AHB Rate if no ratio is set lol
	 */
	if (!ahb_ratio)
		return 200;

	/* Ratio calc from the binary value */
	ahb_ratio++;
	if (cpu_axi_ratio)
		ahb_ratio *= 2;

	logt("clk: ast2600: ahb ratio: %d:1\n", ahb_ratio);

	/* I'm just gonna assume that HCLK = H-PLL / 2 / ahb_ratio */
	hclk = h_pll / 2 / ahb_ratio;
	logt("clk: ast2600: ahb freq: %d MHz\n", hclk);

	return hclk;
}

int ast2600_clk_disable(struct clk *ctx, enum clksrc src)
{
	if (src != clk_arm)
		return -ENOTSUP;

	return scu_writel(ctx->scu, AST2600_SCU_HW_STRAP1,
				    AST2600_SCU_HW_STRAP1_ARM_CLK);
}

int ast2600_clk_enable(struct clk *ctx, enum clksrc src)
{
	if (src != clk_arm)
		return -ENOTSUP;

	return scu_writel(ctx->scu, AST2600_SCU_HW_STRAP1_CLEAR,
				    AST2600_SCU_HW_STRAP1_ARM_CLK);
}

static const struct clk_ops ast2600_clk_ops = {
	.rate_ahb = ast2600_clk_rate_ahb,
	.disable = ast2600_clk_disable,
	.enable = ast2600_clk_enable,
};

static const struct soc_device_id clk_match[] = {
	{ .compatible = "aspeed,ast2400-clock", .data = &ast2400_clk_ops },
	{ .compatible = "aspeed,ast2500-clock", .data = &ast2500_clk_ops },
	{ .compatible = "aspeed,ast2600-clock", .data = &ast2600_clk_ops },
	{ },
};

static int clk_driver_init(struct soc *soc, struct soc_device *dev)
{
	struct clk *ctx;
	int rc;

	ctx = malloc(sizeof(*ctx));
	if (!ctx) {
		return -ENOMEM;
	}

	ctx->scu = scu_get(soc);
	if (!ctx->scu) {
		rc = -ENODEV;
		goto cleanup_ctx;
	}

	ctx->ops = soc_device_get_match_data(soc, clk_match, &dev->node);
	if (!ctx->ops) {
		loge("Failed to find clk ops\n");
		rc = -EINVAL;
		goto cleanup_ctx;
	}

	soc_device_set_drvdata(dev, ctx);

	return 0;

cleanup_ctx:
	free(ctx);

	return rc;
}

static void clk_driver_destroy(struct soc_device *dev)
{
	struct clk *ctx = soc_device_get_drvdata(dev);
	scu_put(ctx->scu);
	free(ctx);
}

static const struct soc_driver clk_driver = {
	.name = "clk",
	.matches = clk_match,
	.init = clk_driver_init,
	.destroy = clk_driver_destroy,
};
REGISTER_SOC_DRIVER(clk_driver);

struct clk *clk_get(struct soc *soc)
{
	return soc_driver_get_drvdata(soc, &clk_driver);
}
