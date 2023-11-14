// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "log.h"
#include "wdt.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

/* Registers */
#define WDT_RELOAD	        0x04
#define WDT_RESTART	        0x08
#define   WDT_RESTART_MAGIC     0x4755
#define WDT_CTRL	        0x0c
#define   WDT_CTRL_ALT_BOOT     (1 << 7)
#define   WDT_CTRL_RESET_SOC    (0b00 << 5)
#define   WDT_CTRL_RESET_SYS    (0b01 << 5)
#define   WDT_CTRL_RESET_CPU    (0b10 << 5)
#define	  WDT_CTRL_CLK_1MHZ	(1 << 4)
#define   WDT_CTRL_SYS_RESET	(1 << 1)
#define   WDT_CTRL_ENABLE	(1 << 0)
#define WDT_RESET_MASK		0x1c

struct wdt {
	struct soc *soc;
	struct soc_region iomem;
	struct clk *clk;
};

static inline int wdt_readl(struct wdt *ctx, uint32_t reg, uint32_t *val)
{
    int rc;
    rc = soc_readl(ctx->soc, ctx->iomem.start + reg, val);
    logt("wdt_readl:\tbase: 0x%08" PRIx32 ", reg: 0x%02" PRIx32 ", val: 0x%08"
            PRIx32 "\n", ctx->iomem.start, reg, *val);
    return rc;
}

static inline int wdt_writel(struct wdt *ctx, uint32_t reg, uint32_t val)
{
    logt("wdt_writel:\tbase: 0x%08" PRIx32 ", reg: 0x%02" PRIx32 ", val: 0x%08"
            PRIx32 "\n", ctx->iomem.start, reg, val);
    return soc_writel(ctx->soc, ctx->iomem.start + reg, val);
}

static int wdt_stop(struct wdt *ctx)
{
    uint32_t val;
    int rc;

    rc = wdt_readl(ctx, WDT_CTRL, &val);
    if (rc < 0)
        return rc;

    val &= ~WDT_CTRL_ENABLE;

    return wdt_writel(ctx, WDT_CTRL, val);
}

static int wdt_config_clksrc(struct wdt *ctx)
{
    uint32_t val;
    int rc;

    rc = wdt_readl(ctx, WDT_CTRL, &val);
    if (rc < 0)
        return rc;

    val |= WDT_CTRL_CLK_1MHZ;

    return wdt_writel(ctx, WDT_CTRL, val);
}

#define AST_WDT_MAX 3

int wdt_prevent_reset(struct soc *soc)
{
    /* FIXME: use for_each over the wdt dt nodes or something... */
    for (int i = 0; i < AST_WDT_MAX; i++) {
        struct wdt _wdt, *wdt = &_wdt;
        char name[] = "wdt1";
        int rc;

        /* ... but for now we YOLO */
        assert(i < 10);
        name[3] = '1' + i;

        if (!(wdt = wdt_get_by_name(soc, name))) {
            logd("Failed to acquire %s controller\n", name);
            return -ENODEV;
        }

        rc = wdt_stop(wdt);
        if (rc < 0)
            return rc;
    }

    return 0;
}

static int64_t wdt_usecs_to_ticks(struct wdt *ctx, uint32_t usecs)
{
    uint32_t val;
    int rc;

    rc = wdt_readl(ctx, WDT_CTRL, &val);
    if (rc < 0)
        return rc;

    /* Don't support PCLK as a source yet, involves scraping around in SCU */
    if (!(val & WDT_CTRL_CLK_1MHZ)) {
        loge("wdt: PCLK source unsupported, bailing\n");
        return (int64_t)-EIO;
    }

    return usecs;
}

int wdt_perform_reset(struct wdt *ctx)
{
    uint32_t mode;
    int64_t wait;
    int rc;

    if ((rc = wdt_stop(ctx)) < 0)
        return rc;

    if ((rc = wdt_config_clksrc(ctx)) < 0)
        return rc;

    /* Reset everything except SPI, X-DMA, MCTP and SDRAM */
    /* Explicitly, reset the AHB bridges */
    rc = wdt_writel(ctx, WDT_RESET_MASK, 0x23ffffb);
    if (rc < 0)
        return rc;

    /* Wait enough time to cover using the debug UART for a reset */
    wait = wdt_usecs_to_ticks(ctx, 5000000);
    if (wait < 0)
        return wait;

    rc = wdt_writel(ctx, WDT_RELOAD, wait);
    if (rc < 0)
        return rc;

    rc = wdt_writel(ctx, WDT_RESTART, WDT_RESTART_MAGIC);
    if (rc < 0)
        return rc;

    if ((rc = wdt_readl(ctx, WDT_CTRL, &mode)) < 0)
        return rc;

    mode |= WDT_CTRL_RESET_SOC | WDT_CTRL_SYS_RESET | WDT_CTRL_ENABLE;
    mode &= ~WDT_CTRL_ALT_BOOT;

    if ((rc = wdt_writel(ctx, WDT_CTRL, mode)) < 0)
        return rc;

    if ((rc = ahb_release_bridge(ctx->soc->ahb)) < 0)
        return rc;

    /*
     * Allow a little extra time for reset to occur (we're timing this
     * asynchronously after all) before we try to reinitialize the bridge
     */
    wait += 1000000;
    logd("Waiting %"PRId64" microseconds for watchdog timer to expire\n", wait);
    usleep(wait);

    if ((rc = ahb_reinit_bridge(ctx->soc->ahb)) < 0) {
        loge("Failed to reinitialize bridge after reset: %d\n", rc);
        return rc;
    }

    /* The ARM clock gate is sticky on reset?! Ensure it's clear  */
    if ((rc = clk_enable(ctx->clk, clk_arm)) < 0)
        return rc;

    rc = wdt_writel(ctx, WDT_RELOAD, 0);
    if (rc < 0)
        return rc;

    return 0;
}

static const struct soc_device_id wdt_match[] = {
    { .compatible = "aspeed,ast2500-wdt" },
    { },
};

static int wdt_driver_init(struct soc *soc, struct soc_device *dev)
{
    struct wdt *ctx;
    int rc;

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return -ENOMEM;
    }

    if ((rc = soc_device_get_memory(soc, &dev->node, &ctx->iomem)) < 0) {
        goto cleanup_ctx;
    }

    if (!(ctx->clk = clk_get(soc))) {
        loge("Failed to acquire clock controller\n");
        rc = -ENODEV;
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
