// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "log.h"
#include "wdt.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/* Registers */
#define WDT_RELOAD	        0x04
#define WDT_CTRL	        0x0c
#define   WDT_CTRL_BOOT_1       (0 << 7)
#define   WDT_CTRL_BOOT_2       (1 << 7)
#define   WDT_CTRL_RESET_SOC    (0b00 << 5)
#define   WDT_CTRL_RESET_SYS    (0b01 << 5)
#define   WDT_CTRL_RESET_CPU    (0b10 << 5)
#define	  WDT_CTRL_CLK_1MHZ	(1 << 4)
#define   WDT_CTRL_SYS_RESET	(1 << 1)
#define   WDT_CTRL_ENABLE	(1 << 0)
#define WDT_RESET_MASK		0x1c

static const struct soc_device_id wdt_match[] = {
    { .compatible = "aspeed,ast2500-wdt" },
    { },
};

int wdt_init(struct wdt *ctx, struct soc *soc, const char *name)
{
    struct soc_device_node dn;
    int rc;

    if ((rc = soc_device_from_name(soc, name, &dn)) < 0) {
        loge("wdt: Failed to find device by name '%s': %d\n", name, rc);
        return rc;
    }

    rc = soc_device_is_compatible(soc, wdt_match, &dn);
    if (rc < 0) {
        loge("wdt: Failed verify device compatibility: %d\n", rc);
        return rc;
    }

    if (!rc) {
        loge("wdt: Incompatible device described by node '%s'\n", name);
        return -EINVAL;
    }

    if ((rc = soc_device_get_memory(soc, &dn, &ctx->iomem)) < 0)
        return rc;

    if ((rc = clk_init(&ctx->clk, soc)) < 0)
        return rc;

    ctx->soc = soc;

    return 0;
}

void wdt_destroy(struct wdt *ctx)
{
    clk_destroy(&ctx->clk);
    ctx->soc = NULL;
}

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
        rc = wdt_init(wdt, soc, name);
        if (rc < 0)
            return rc;

        rc = wdt_stop(wdt);
        if (rc < 0)
            return rc;

        wdt_destroy(wdt);
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

int64_t wdt_perform_reset(struct wdt *ctx)
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

    if ((rc = wdt_readl(ctx, WDT_CTRL, &mode)) < 0)
        return rc;

    mode |= WDT_CTRL_RESET_SOC | WDT_CTRL_SYS_RESET | WDT_CTRL_ENABLE;

    if ((rc = wdt_writel(ctx, WDT_CTRL, mode)) < 0)
        return rc;

    /* The ARM clock gate is sticky on reset?! Ensure it's clear  */
    if ((rc = clk_enable(&ctx->clk, clk_arm)) < 0)
        return rc;

    rc = wdt_writel(ctx, WDT_RELOAD, 0);
    if (rc < 0)
        return rc;

    return wait;
}
