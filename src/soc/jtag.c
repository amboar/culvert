// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024 Sarah Maedel

#include <errno.h>

#include "jtag.h"
#include "scu.h"
#include "soc.h"

/* ASPEED Register definitions */
#define AST_JTAG_DATA           0x00
#define AST_JTAG_INST           0x04
#define AST_JTAG_EC             0x08
#define AST_JTAG_ISR            0x0C
#define AST_JTAG_SW_MODE        0x10
#define AST_JTAG_TCK            0x14
#define AST_JTAG_EC1            0x18

/* AST_JTAG_EC/JTAG08: Engine control */
#define AST_JTAG_EC_ENG_EN              BIT(31)
#define AST_JTAG_EC_ENG_OUT_EN          BIT(30)
#define AST_JTAG_EC_FORCE_TMS           BIT(29)

/* AST_JTAG_SW_MODE/JTAG10: Software mode and status */
#define AST_JTAG_SW_MODE_EN             BIT(19)
#define AST_JTAG_SW_MODE_TCK            BIT(18)
#define AST_JTAG_SW_MODE_TMS            BIT(17)
#define AST_JTAG_SW_MODE_TDIO           BIT(16)

#define AST2400_SCU_RESET_CTRL          0x04
#define AST2600_SCU_RESET_CTRL          0x40
#define   SCU_RESET_CTRL_JTAG_MASTER    BIT(22)

#define AST2400_SCU_MISC_CTRL           0x2c
#define AST2600_SCU_MISC_CTRL           0xc0
#define   SCU_MISC_CTRL_JTAG_MASK       (BIT(15) | BIT(14))

struct jtag_ops {
        int (*release)(struct jtag *ctx);
        int (*route)(struct jtag *ctx, uint32_t route);
};

struct jtag {
        const struct jtag_ops *ops;
        struct soc_region regs;
        struct scu *scu;
        struct soc *soc;
        int refcnt;
};

static int jtag_readl(struct jtag *ctx, uint32_t reg, uint32_t *value)
{
        return soc_readl(ctx->soc, ctx->regs.start + reg, value);
}

static int jtag_writel(struct jtag *ctx, uint32_t reg, uint32_t value)
{
        return soc_writel(ctx->soc, ctx->regs.start + reg, value);
}

int jtag_route(struct jtag *ctx, uint32_t route)
{
        if (route & ~SCU_MISC_CTRL_JTAG_MASK) {
                return -EINVAL;
        }

        return ctx->ops->route(ctx, route);
}

int jtag_bitbang_set(struct jtag *ctx, uint8_t tck, uint8_t tms, uint8_t tdi)
{
        return jtag_writel(ctx, AST_JTAG_SW_MODE,
                           AST_JTAG_SW_MODE_EN |
                           ((!!tck) * AST_JTAG_SW_MODE_TCK) |
                           ((!!tms) * AST_JTAG_SW_MODE_TMS) |
                           ((!!tdi) * AST_JTAG_SW_MODE_TDIO));
}

int jtag_bitbang_get(struct jtag *ctx, uint8_t* tdo)
{
        uint32_t reg = 0;
        int rc = jtag_readl(ctx, AST_JTAG_SW_MODE, &reg);

        *tdo = !!(reg & AST_JTAG_SW_MODE_TDIO);

        return rc;
}

static int ast2400_jtag_release(struct jtag *ctx)
{
        uint32_t reg;
        int rc;

        if ((rc = scu_readl(ctx->scu, AST2400_SCU_RESET_CTRL, &reg)) < 0)
            return rc;

        reg &= ~SCU_RESET_CTRL_JTAG_MASTER;

        if ((rc = scu_writel(ctx->scu, AST2400_SCU_RESET_CTRL, reg)) < 0)
            return rc;

        return 0;
}

static int ast2400_jtag_route(struct jtag *ctx, uint32_t route)
{
        uint32_t reg;
        int rc;

        if ((rc = scu_readl(ctx->scu, AST2400_SCU_MISC_CTRL, &reg)) < 0)
            return rc;

        reg &= ~(uint32_t)SCU_MISC_CTRL_JTAG_MASK;
        reg |= route;

        if ((rc = scu_writel(ctx->scu, AST2400_SCU_MISC_CTRL, reg)) < 0)
            return rc;

        return 0;
}

static const struct jtag_ops ast2400_jtag_ops = {
        .release = ast2400_jtag_release,
        .route = ast2400_jtag_route
};

static int ast2600_jtag_release(struct jtag *ctx)
{
        return scu_writel(ctx->scu, AST2600_SCU_RESET_CTRL + 4,
                          SCU_RESET_CTRL_JTAG_MASTER);
}

static int ast2600_jtag_route(struct jtag *ctx, uint32_t route)
{
        uint32_t reg;
        int rc;

        if ((rc = scu_readl(ctx->scu, AST2600_SCU_MISC_CTRL, &reg)) < 0)
            return rc;

        reg &= ~(uint32_t)SCU_MISC_CTRL_JTAG_MASK;
        reg |= route;

        if ((rc = scu_writel(ctx->scu, AST2600_SCU_MISC_CTRL, reg)) < 0)
            return rc;

        return 0;
}

static const struct jtag_ops ast2600_jtag_ops = {
        .release = ast2600_jtag_release,
        .route = ast2600_jtag_route,
};

static const struct soc_device_id jtag_match[] = {
        { .compatible = "aspeed,ast2400-jtag", .data = &ast2400_jtag_ops },
        { .compatible = "aspeed,ast2500-jtag", .data = &ast2400_jtag_ops },
        { .compatible = "aspeed,ast2600-jtag", .data = &ast2600_jtag_ops },
        { },
};

static int jtag_driver_init(struct soc *soc, struct soc_device *dev)
{
        struct jtag *ctx;
        int rc;

        ctx = malloc(sizeof(*ctx));
        if (!ctx) {
                return -ENOMEM;
        }

        ctx->ops = soc_device_get_match_data(soc, jtag_match, &dev->node);
        if (!ctx->ops || !ctx->ops->release || !ctx->ops->route) {
                rc = -EINVAL;
                goto cleanup_ctx;
        }

        if ((rc = soc_device_get_memory(soc, &dev->node, &ctx->regs)) < 0) {
                goto cleanup_ctx;
        }
        ctx->refcnt = 1;
        ctx->soc = soc;

        ctx->scu = scu_get(soc);
        if (!ctx->scu) {
                rc = -ENODEV;
                goto cleanup_ctx;
        }

        // take JTAG master out of reset 
        if ((rc = ctx->ops->release(ctx)) < 0) {
                goto cleanup_ctx;
        }

        // enable JTAG master controller
        if ((rc = jtag_writel(ctx, AST_JTAG_EC, 
                             AST_JTAG_EC_ENG_EN | 
                             AST_JTAG_EC_ENG_OUT_EN)) < 0) {
                goto cleanup_ctx;
        }

        // reset JTAG master controller (peripheral clears bit itself)
        if ((rc = jtag_writel(ctx, AST_JTAG_EC, 
                             AST_JTAG_EC_ENG_EN | 
                             AST_JTAG_EC_ENG_OUT_EN | 
                             AST_JTAG_EC_FORCE_TMS)) < 0) {
                goto cleanup_ctx;
        }

        // enable software JTAG mode/bitbang
        if ((rc = jtag_writel(ctx, AST_JTAG_SW_MODE, AST_JTAG_SW_MODE_EN)) < 0) {
                goto cleanup_ctx;
        }

        soc_device_set_drvdata(dev, ctx);

        return 0;

cleanup_ctx:
        free(ctx);
        return rc;
}

static void jtag_driver_destroy(struct soc_device *dev)
{
        jtag_put(soc_device_get_drvdata(dev));
}

static const struct soc_driver jtag_driver = {
        .name = "jtag",
        .matches = jtag_match,
        .init = jtag_driver_init,
        .destroy = jtag_driver_destroy,
};
REGISTER_SOC_DRIVER(jtag_driver);

struct jtag *jtag_get(struct soc *soc, const char *name)
{
        struct jtag *ctx = soc_driver_get_drvdata_by_name(soc, &jtag_driver, name);
        if (ctx) {
                ctx->refcnt += 1;
        }
        return ctx;
}

void jtag_put(struct jtag *ctx)
{
        ctx->refcnt -= 1;
        if (!ctx->refcnt) {
                free(ctx);
        }
}
