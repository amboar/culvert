// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2023 IBM Corp.

#include "log.h"
#include "soc/strap.h"

#include <errno.h>

struct strap_ops {
    int (*read)(struct strap *ctx, int reg, uint32_t *val);
    int (*set)(struct strap *ctx, int reg, uint32_t update, uint32_t mask);
    int (*clear)(struct strap *ctx, int reg, uint32_t update, uint32_t mask);
};

struct strap {
    struct soc *soc;
    struct soc_region scu;
    const struct strap_ops *ops;
};

int strap_read(struct strap *ctx, int reg, uint32_t *val)
{
    return ctx->ops->read(ctx, reg, val);
}

int strap_set(struct strap *ctx, int reg, uint32_t update, uint32_t mask)
{
    return ctx->ops->set(ctx, reg, update, mask);
}

int strap_clear(struct strap *ctx, int reg, uint32_t update, uint32_t mask)
{
    return ctx->ops->clear(ctx, reg, update, mask);
}

static int scu_readl(struct strap *ctx, uint32_t reg, uint32_t *val)
{
    return soc_readl(ctx->soc, ctx->scu.start + reg, val);
}

static int scu_writel(struct strap *ctx, uint32_t reg, uint32_t val)
{
    return soc_writel(ctx->soc, ctx->scu.start + reg, val);
}

static int ast2400_strap_read(struct strap *ctx, int reg, uint32_t *val)
{
    if (!(reg == AST2400_SCU_HW_STRAP1 || reg == AST2400_SCU_HW_STRAP2)) {
        return -EINVAL;
    }

    return scu_readl(ctx, reg, val);
}

static int ast2400_strap_set(struct strap *ctx, int reg, uint32_t update, uint32_t mask)
{
    uint32_t val;
    int rc;

    if (!(reg == AST2400_SCU_HW_STRAP1 || reg == AST2400_SCU_HW_STRAP2)) {
        return -EINVAL;
    }

    if (update & ~mask) {
        return -EINVAL;
    }

    if ((rc = scu_readl(ctx, reg, &val)) < 0) {
        return rc;
    }

    val |= update;

    return scu_writel(ctx, reg, val);
}

static int ast2400_strap_clear(struct strap *ctx, int reg, uint32_t update, uint32_t mask)
{
    uint32_t val;
    int rc;

    if (!(reg == AST2400_SCU_HW_STRAP1 || reg == AST2400_SCU_HW_STRAP2)) {
        return -EINVAL;
    }

    if (update & ~mask) {
        return -EINVAL;
    }

    if ((rc = scu_readl(ctx, reg, &val)) < 0) {
        return rc;
    }

    val &= ~update;

    return scu_writel(ctx, reg, val);
}

static const struct strap_ops ast2400_strap_ops = {
    .read = ast2400_strap_read,
    .set = ast2400_strap_set,
    .clear = ast2400_strap_clear,
};

static int ast2500_strap_read(struct strap *ctx, int reg, uint32_t *val)
{
    if (reg != AST2500_SCU_HW_STRAP) {
        return -EINVAL;
    }

    return scu_readl(ctx, reg, val);
}

static int ast2500_strap_set(struct strap *ctx, int reg, uint32_t update, uint32_t mask)
{
    if ((reg != AST2500_SCU_HW_STRAP) || (update & ~mask)) {
        return -EINVAL;
    }

    return scu_writel(ctx, reg, update);
}

static int ast2500_strap_clear(struct strap *ctx, int reg, uint32_t update, uint32_t mask)
{
    if ((reg != AST2500_SCU_HW_STRAP) || (update & ~mask)) {
        return -EINVAL;
    }

    /* The silicon ID register is W1C for the strap register */
    return scu_writel(ctx, AST2500_SCU_SILICON_ID, update);
}

static const struct strap_ops ast2500_strap_ops = {
    .read = ast2500_strap_read,
    .set = ast2500_strap_set,
    .clear = ast2500_strap_clear,
};

static int ast2600_strap_read(struct strap *ctx, int reg, uint32_t *val)
{
    if (!(reg == AST2600_SCU_HW_STRAP1 ||
                reg == AST2600_SCU_HW_STRAP2 ||
                reg == AST2600_SCU_HW_STRAP3)) {
        return -EINVAL;
    }

    return scu_readl(ctx, reg, val);
}

static int ast2600_strap_is_protected(struct strap *ctx, int reg, uint32_t mask)
{
    /* STRAP3 is RMW with no protection */
    if (reg == AST2600_SCU_HW_STRAP3) {
        return 0;
    }

    /* STRAP1 and STRAP2 are W1S/W1C with write protection */
    if (reg == AST2600_SCU_HW_STRAP1 || reg == AST2600_SCU_HW_STRAP2) {
        uint32_t protect;
        int rc;

        if ((rc = scu_readl(ctx, reg + 8, &protect)) < 0) {
            loge("Failed to read protection register for strapping register %d: %d\n", reg, rc);
            return rc;
        }

        return !!(protect & mask);
    }

    return -EINVAL;
}

static int ast2600_strap_set(struct strap *ctx, int reg, uint32_t update, uint32_t mask)
{
    int rc;

    if (!(reg == AST2600_SCU_HW_STRAP1 ||
                reg == AST2600_SCU_HW_STRAP2 ||
                reg == AST2600_SCU_HW_STRAP3)) {
        return -EINVAL;
    }

    if (update & ~mask) {
        return -EINVAL;
    }

    if ((rc = ast2600_strap_is_protected(ctx, reg, mask)) < 0) {
        return rc;
    } else if (rc) {
        return -EPERM;
    }

    return scu_writel(ctx, reg, update);
}

static int ast2600_strap_clear(struct strap *ctx, int reg, uint32_t update, uint32_t mask)
{
    int rc;

    if (update & ~mask) {
        return -EINVAL;
    }

    if ((rc = ast2600_strap_is_protected(ctx, reg, mask)) < 0) {
        return rc;
    } else if (rc) {
        return -EPERM;
    }

    /* W1S/W1C pairs */
    if (reg == AST2600_SCU_HW_STRAP1 || reg == AST2600_SCU_HW_STRAP2) {
        return scu_writel(ctx, reg + 4, update);
    }

    /* RMW, because register layout is hard */
    if (reg == AST2600_SCU_HW_STRAP3) {
        uint32_t val;
        int rc;

        if ((rc = scu_readl(ctx, reg, &val)) < 0) {
            return rc;
        }

        return scu_writel(ctx, reg, val & ~(update & mask));
    }

    return -EINVAL;
}

static const struct strap_ops ast2600_strap_ops = {
    .read = ast2600_strap_read,
    .set = ast2600_strap_set,
    .clear = ast2600_strap_clear,
};

static const struct soc_device_id strap_matches[] = {
    { .compatible = "aspeed,ast2400-strapping", .data = &ast2400_strap_ops },
    { .compatible = "aspeed,ast2500-strapping", .data = &ast2500_strap_ops },
    { .compatible = "aspeed,ast2600-strapping", .data = &ast2600_strap_ops },
    { },
};

static int strap_driver_init(struct soc *soc, struct soc_device *dev)
{
    struct strap *ctx;
    int rc;

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return -ENOMEM;
    }

    if ((rc = soc_device_get_memory(soc, &dev->node, &ctx->scu)) < 0) {
        goto cleanup_ctx;
    }

    ctx->soc = soc;
    ctx->ops = soc_device_get_match_data(soc, strap_matches, &dev->node);

    soc_device_set_drvdata(dev, ctx);

    return 0;

cleanup_ctx:
    free(ctx);

    return rc;
}

static void strap_driver_destroy(struct soc_device *dev)
{
    free(soc_device_get_drvdata(dev));
}

static const struct soc_driver strap_driver = {
    .name = "strap",
    .matches = strap_matches,
    .init = strap_driver_init,
    .destroy = strap_driver_destroy,
};
REGISTER_SOC_DRIVER(strap_driver);

struct strap *strap_get(struct soc *soc)
{
    return soc_driver_get_drvdata(soc, &strap_driver);
}
