// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "bits.h"
#include "log.h"
#include "wdt.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

/* Registers */
#define AST2500_WDT_RELOAD		 0x04
#define AST2500_WDT_RESTART		 0x08
#define AST2500_WDT_RESTART_MAGIC	 0x4755
#define AST2500_WDT_CTRL		 0x0c
#define AST2500_WDT_CTRL_ALT_BOOT	 BIT(7)
#define AST2500_WDT_CTRL_RESET_MODE_MASK GENMASK(6, 5)
#define AST2500_WDT_CTRL_RESET_SOC \
	FIELD_PREP(AST2500_WDT_CTRL_RESET_MODE_MASK, 0b00)
#define AST2500_WDT_CTRL_RESET_SYS \
	FIELD_PREP(AST2500_WDT_CTRL_RESET_MODE_MASK, 0b01)
#define AST2500_WDT_CTRL_RESET_CPU \
	FIELD_PREP(AST2500_WDT_CTRL_RESET_MODE_MASK, 0b10)
#define AST2500_WDT_CTRL_CLK_1MHZ  BIT(4)
#define AST2500_WDT_CTRL_SYS_RESET BIT(1)
#define AST2500_WDT_CTRL_ENABLE	   BIT(0)
#define AST2500_WDT_RESET_MASK	   0x1c

#define AST2600_WDT_RELOAD		 0x04
#define AST2600_WDT_RESTART		 0x08
#define AST2600_WDT_RESTART_MAGIC	 0x4755
#define AST2600_WDT_CTRL		 0x0c
#define AST2600_WDT_CTRL_RESET_MODE_MASK GENMASK(6, 5)
#define AST2600_WDT_CTRL_RESET_SOC \
	FIELD_PREP(AST2600_WDT_CTRL_RESET_MODE_MASK, 0b00)
#define AST2600_WDT_CTRL_RESET_FULL \
	FIELD_PREP(AST2600_WDT_CTRL_RESET_MODE_MASK, 0b01)
#define AST2600_WDT_CTRL_RESET_CPU \
	FIELD_PREP(AST2600_WDT_CTRL_RESET_MODE_MASK, 0b10)
#define AST2600_WDT_CTRL_RESET_BY_SOC	   BIT(4)
#define AST2600_WDT_CTRL_SYS_RESET	   BIT(1)
#define AST2600_WDT_CTRL_ENABLE		   BIT(0)
#define AST2600_WDT_TIMEOUT_STATUS	   0x10
#define AST2600_WDT_TIMEOUT_STATUS_TIMEOUT BIT(0)
#define AST2600_WDT_TIMEOUT_STATUS_CLR	   0x14
#define AST2600_WDT_RESET_MASK1		   0x1c
#define AST2600_WDT_RESET_MASK1_SDRAM	   BIT(1)
#define AST2600_WDT_RESET_MASK2		   0x20
#define AST2600_WDT_RESET_MASK2_SPI	   BIT(1)

static const struct soc_driver wdt_driver;

struct wdt_ops {
	int (*wdt_perform_reset)(struct wdt *ctx);
	int (*stop)(struct wdt *ctx);
};

struct wdt {
	struct soc *soc;
	const struct wdt_ops *ops;
	struct soc_region iomem;
	struct clk *clk;
};

int wdt_perform_reset(struct wdt *ctx)
{
	return ctx->ops->wdt_perform_reset(ctx);
}

int wdt_prevent_reset(struct soc *soc)
{
	struct soc_device *dev;

	list_for_each(&soc->devices, dev, entry) {
		struct wdt *wdt;
		int rc;

		if (dev->driver != &wdt_driver)
			continue;

		wdt = soc_driver_get_drvdata_by_node(soc, &dev->node);
		if (!wdt)
			return -ENODEV;

		rc = wdt->ops->stop(wdt);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static inline int wdt_readl(struct wdt *ctx, uint32_t reg, uint32_t *val)
{
	int rc;
	rc = soc_readl(ctx->soc, ctx->iomem.start + reg, val);
	logt("wdt_readl:\tbase: 0x%08" PRIx32 ", reg: 0x%02" PRIx32
	     ", val: 0x%08" PRIx32 "\n",
	     ctx->iomem.start, reg, *val);
	return rc;
}

static inline int wdt_writel(struct wdt *ctx, uint32_t reg, uint32_t val)
{
	logt("wdt_writel:\tbase: 0x%08" PRIx32 ", reg: 0x%02" PRIx32
	     ", val: 0x%08" PRIx32 "\n",
	     ctx->iomem.start, reg, val);
	return soc_writel(ctx->soc, ctx->iomem.start + reg, val);
}

static int ast2500_wdt_stop(struct wdt *ctx)
{
	uint32_t val;
	int rc;

	rc = wdt_readl(ctx, AST2500_WDT_CTRL, &val);
	if (rc < 0)
		return rc;

	val &= ~AST2500_WDT_CTRL_ENABLE;

	return wdt_writel(ctx, AST2500_WDT_CTRL, val);
}

static int ast2500_wdt_config_clksrc(struct wdt *ctx)
{
	uint32_t val;
	int rc;

	rc = wdt_readl(ctx, AST2500_WDT_CTRL, &val);
	if (rc < 0)
		return rc;

	val |= AST2500_WDT_CTRL_CLK_1MHZ;

	return wdt_writel(ctx, AST2500_WDT_CTRL, val);
}

static int64_t ast2500_wdt_usecs_to_ticks(struct wdt *ctx, uint32_t usecs)
{
	uint32_t val;
	int rc;

	rc = wdt_readl(ctx, AST2500_WDT_CTRL, &val);
	if (rc < 0)
		return rc;

	/* Don't support PCLK as a source yet, involves scraping around in SCU */
	if (!(val & AST2500_WDT_CTRL_CLK_1MHZ)) {
		loge("wdt: PCLK source unsupported, bailing\n");
		return (int64_t)-EIO;
	}

	return usecs;
}

int ast2500_wdt_perform_reset(struct wdt *ctx)
{
	uint32_t mode;
	int64_t wait;
	int rc;

	if ((rc = ast2500_wdt_stop(ctx)) < 0)
		return rc;

	if ((rc = ast2500_wdt_config_clksrc(ctx)) < 0)
		return rc;

	/* Reset everything except SPI, X-DMA, MCTP and SDRAM */
	/* Explicitly, reset the AHB bridges */
	rc = wdt_writel(ctx, AST2500_WDT_RESET_MASK, 0x23ffffb);
	if (rc < 0)
		return rc;

	/* Wait enough time to cover using the debug UART for a reset */
	wait = ast2500_wdt_usecs_to_ticks(ctx, 5000000);
	if (wait < 0)
		return wait;

	rc = wdt_writel(ctx, AST2500_WDT_RELOAD, wait);
	if (rc < 0)
		return rc;

	rc = wdt_writel(ctx, AST2500_WDT_RESTART, AST2500_WDT_RESTART_MAGIC);
	if (rc < 0)
		return rc;

	if ((rc = wdt_readl(ctx, AST2500_WDT_CTRL, &mode)) < 0)
		return rc;

	mode |= AST2500_WDT_CTRL_RESET_SOC | AST2500_WDT_CTRL_SYS_RESET |
		AST2500_WDT_CTRL_ENABLE;
	mode &= ~AST2500_WDT_CTRL_ALT_BOOT;

	if ((rc = wdt_writel(ctx, AST2500_WDT_CTRL, mode)) < 0)
		return rc;

	if ((rc = ahb_release_bridge(ctx->soc->ahb)) < 0)
		return rc;

	/*
	 * Allow a little extra time for reset to occur (we're timing this
	 * asynchronously after all) before we try to reinitialize the bridge
	 */
	wait += 1000000;
	logd("Waiting %" PRId64 " microseconds for watchdog timer to expire\n",
	     wait);
	usleep(wait);

	if ((rc = ahb_reinit_bridge(ctx->soc->ahb)) < 0) {
		loge("Failed to reinitialize bridge after reset: %d\n", rc);
		return rc;
	}

	/* The ARM clock gate is sticky on reset?! Ensure it's clear  */
	if ((rc = clk_enable(ctx->clk, clk_arm)) < 0)
		return rc;

	rc = wdt_writel(ctx, AST2500_WDT_RELOAD, 0);
	if (rc < 0)
		return rc;

	return 0;
}

static const struct wdt_ops ast2500_wdt_ops = {
	.wdt_perform_reset = ast2500_wdt_perform_reset,
	.stop = ast2500_wdt_stop,
};

static int ast2600_wdt_stop(struct wdt *ctx)
{
	uint32_t val;
	int rc;

	rc = wdt_readl(ctx, AST2600_WDT_CTRL, &val);
	if (rc < 0)
		return rc;

	val &= ~AST2600_WDT_CTRL_ENABLE;

	return wdt_writel(ctx, AST2600_WDT_CTRL, val);
}

static int ast2600_wdt_perform_reset(struct wdt *ctx)
{
	uint32_t mode;
	int64_t wait;
	int rc;

	if ((rc = ast2600_wdt_stop(ctx)) < 0)
		return rc;

	if ((rc = wdt_writel(ctx, AST2600_WDT_RESET_MASK1,
			     GENMASK(25, 0) & ~AST2600_WDT_RESET_MASK1_SDRAM)) <
	    0)
		return rc;

	if ((rc = wdt_writel(ctx, AST2600_WDT_RESET_MASK2,
			     (GENMASK(27, 0) & ~GENMASK(25, 24)) &
				     ~AST2600_WDT_RESET_MASK2_SPI)) < 0)
		return rc;

	wait = 5000000;

	if ((rc = wdt_writel(ctx, AST2600_WDT_RELOAD, wait)) < 0)
		return rc;

	if ((rc = wdt_writel(ctx, AST2600_WDT_RESTART,
			     AST2600_WDT_RESTART_MAGIC)) < 0)
		return rc;

	if ((rc = wdt_readl(ctx, AST2600_WDT_CTRL, &mode)) < 0)
		return rc;

	mode &= ~AST2600_WDT_CTRL_RESET_MODE_MASK;
	mode |= AST2600_WDT_CTRL_RESET_SOC | AST2600_WDT_CTRL_SYS_RESET |
		AST2600_WDT_CTRL_ENABLE;

	if ((rc = wdt_writel(ctx, AST2600_WDT_CTRL, mode)) < 0)
		return rc;

	if ((rc = ahb_release_bridge(ctx->soc->ahb)) < 0)
		return rc;

	/*
	 * Allow a little extra time for reset to occur (we're timing this
	 * asynchronously after all) before we try to reinitialize the bridge
	 */
	wait += 1000000;
	logd("Waiting %" PRId64 " microseconds for watchdog timer to expire\n",
	     wait);
	usleep(wait);

	if ((rc = ahb_reinit_bridge(ctx->soc->ahb)) < 0) {
		loge("Failed to reinitialize bridge after reset: %d\n", rc);
		return rc;
	}

	/* The ARM clock gate is sticky on reset?! Ensure it's clear */
	if ((rc = clk_enable(ctx->clk, clk_arm)) < 0)
		return rc;

	if ((rc = wdt_writel(ctx, AST2600_WDT_RELOAD, 0)) < 0)
		return rc;

	return 0;
}

static const struct wdt_ops ast2600_wdt_ops = {
	.wdt_perform_reset = ast2600_wdt_perform_reset,
	.stop = ast2600_wdt_stop,
};

static const struct soc_device_id wdt_match[] = {
	{ .compatible = "aspeed,ast2500-wdt", .data = &ast2500_wdt_ops },
	{ .compatible = "aspeed,ast2600-wdt", .data = &ast2600_wdt_ops },
	{},
};

static int wdt_driver_init(struct soc *soc, struct soc_device *dev)
{
	struct wdt *ctx;
	int rc;

	ctx = malloc(sizeof(*ctx));
	if (!ctx)
		return -ENOMEM;

	if ((rc = soc_device_get_memory(soc, &dev->node, &ctx->iomem)) < 0)
		goto cleanup_ctx;

	if (!(ctx->clk = clk_get(soc))) {
		loge("Failed to acquire clock controller\n");
		rc = -ENODEV;
		goto cleanup_ctx;
	}

	ctx->ops = soc_device_get_match_data(soc, wdt_match, &dev->node);
	if (!ctx->ops) {
		loge("Failed to find wdt ops\n");
		rc = -EINVAL;
		goto cleanup_ctx;
	}

	ctx->soc = soc;

	soc_device_set_drvdata(dev, ctx);

	return 0;

cleanup_ctx:
	free(ctx);

	return rc;
}

static void wdt_driver_destroy(struct soc_device *dev)
{
	free(soc_device_get_drvdata(dev));
}

static const struct soc_driver wdt_driver = {
	.name = "wdt",
	.matches = wdt_match,
	.init = wdt_driver_init,
	.destroy = wdt_driver_destroy,
};
REGISTER_SOC_DRIVER(wdt_driver);

struct wdt *wdt_get_by_name(struct soc *soc, const char *name)
{
	return soc_driver_get_drvdata_by_name(soc, &wdt_driver, name);
}
