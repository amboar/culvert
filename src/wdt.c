// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "ast.h"
#include "wdt.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

enum wdt { wdt1, wdt2, wdt3 };

#define AST_G5_WDTS (wdt3 + 1)

static inline uint32_t wdt_addr(enum wdt wdt, uint32_t reg)
{
    assert(reg < WDT_SIZE);
    return AST_G5_WDT | (wdt * WDT_SIZE) | reg;
}

static int wdt_stop(struct ahb *ahb, enum wdt wdt)
{
    uint32_t ctrl;
    uint32_t val;
    int rc;

    ctrl = wdt_addr(wdt, WDT_CTRL);
    rc = ahb_readl(ahb, ctrl, &val);
    if (rc < 0)
        return rc;

    val &= ~WDT_CTRL_ENABLE;

    return ahb_writel(ahb, ctrl, val);
}

int wdt_prevent_reset(struct ahb *ahb)
{
    for (int i = 0; i < AST_G5_WDTS; i++) {
        int rc;

        rc = wdt_stop(ahb, i);
        if (rc < 0)
            return rc;
    }

    return 0;
}

static uint32_t wdt_usecs_to_ticks(struct ahb *ahb, enum wdt wdt,
                                   uint32_t usecs)
{
    uint32_t val;
    int rc;

    rc = ahb_readl(ahb, wdt_addr(wdt, WDT_CTRL), &val);
    if (rc < 0)
        return rc;

    /* Don't support PCLK as a source yet, involves scraping around in SCU */
    assert(val & WDT_CTRL_CLK_1MHZ);

    return usecs;
}

int64_t wdt_perform_reset(struct ahb *ahb)
{
    enum wdt victim = wdt2;
    uint32_t mode;
    int64_t wait;
    int rc;

    rc = wdt_stop(ahb, victim);
    if (rc < 0)
        return rc;

    /* Reset everything except SPI, X-DMA, MCTP and SDRAM */
    /* Explicitly, reset the AHB bridges */
    rc = ahb_writel(ahb, wdt_addr(victim, WDT_RESET_MASK), 0x23ffffb);
    if (rc < 0)
        return rc;

    /* Wait enough time to cover using the debug UART for a reset */
    wait = wdt_usecs_to_ticks(ahb, victim, 5000000);
    if (wait < 0)
        return wait;

    rc = ahb_writel(ahb, wdt_addr(victim, WDT_RELOAD), wait);
    if (rc < 0)
        return rc;

    mode = WDT_CTRL_RESET_SOC
            | WDT_CTRL_CLK_1MHZ
            | WDT_CTRL_SYS_RESET
            | WDT_CTRL_ENABLE;

    rc = ahb_writel(ahb, wdt_addr(victim, WDT_CTRL), mode);
    if (rc < 0)
        return rc;

    /* The ARM clock gate is sticky on reset?! Ensure it's clear  */
    rc = ahb_writel(ahb, AST_G5_SCU | SCU_SILICON_REVISION,
                      SCU_HW_STRAP_ARM_CLK);
    if (rc < 0)
        return rc;

    rc = ahb_writel(ahb, wdt_addr(victim, WDT_RELOAD), 0);
    if (rc < 0)
        return rc;

    return wait;
}
