// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2022 IBM Corp.

#include "log.h"
#include "soc/sioctl.h"
#include "soc/strap.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#define G4_SCU_HW_STRAP                 0x070
#define   G4_SCU_HW_STRAP_SIO_DEC	(1 << 20)
#define   G4_SCU_HW_STRAP_SIO_4E        (1 << 16)
#define G6_SCU_HW_STRAP2                0x510
#define   G6_SCU_HW_STRAP2_SIO_DEC      (1 << 3)
#define   G6_SCU_HW_STRAP2_SIO_4E       (1 << 2)

#include "soc/strap.h"

struct sioctl_pdata {
    uint32_t reg;
    uint32_t disable;
    uint32_t select;
};

struct sioctl {
    struct soc *soc;
    struct soc_region scu;
    struct strap *strap;
    const struct sioctl_pdata *pdata;
};

int sioctl_decode_configure(struct sioctl *ctx, const enum sioctl_decode mode)
{
    int rc;

    if (mode == sioctl_decode_disable) {
        return strap_set(ctx->strap, ctx->pdata->reg, ctx->pdata->disable, ctx->pdata->disable);
    }

    if (mode == sioctl_decode_4e) {
        rc = strap_set(ctx->strap, ctx->pdata->reg, ctx->pdata->select, ctx->pdata->select);
        if (rc < 0) {
            return rc;
        }
    } else {
        assert(mode == sioctl_decode_2e);
        rc = strap_clear(ctx->strap, ctx->pdata->reg, ctx->pdata->select, ctx->pdata->select);
        if (rc < 0) {
            return rc;
        }
    }

    return strap_clear(ctx->strap, ctx->pdata->reg, ctx->pdata->disable, ctx->pdata->disable);
}

int sioctl_decode_status(struct sioctl *ctx, enum sioctl_decode *status)
{
    uint32_t strap;
    int rc;

    if ((rc = strap_read(ctx->strap, ctx->pdata->reg, &strap)) < 0) {
        return rc;
    }

    if (strap & ctx->pdata->disable) {
        *status = sioctl_decode_disable;

        return 0;
    }

    *status = (strap & ctx->pdata->select) ? sioctl_decode_4e : sioctl_decode_2e;

    return 0;
}

static const struct sioctl_pdata ast2400_sioctl_pdata = {
    .reg = G4_SCU_HW_STRAP,
    .disable = G4_SCU_HW_STRAP_SIO_DEC,
    .select = G4_SCU_HW_STRAP_SIO_4E,
};

static const struct sioctl_pdata ast2600_sioctl_pdata = {
    .reg = G6_SCU_HW_STRAP2,
    .disable = G6_SCU_HW_STRAP2_SIO_DEC,
    .select = G6_SCU_HW_STRAP2_SIO_4E,
};

static const struct soc_device_id sioctl_matches[] = {
    { .compatible = "aspeed,ast2400-superio", .data = &ast2400_sioctl_pdata },
    { .compatible = "aspeed,ast2500-superio", .data = &ast2400_sioctl_pdata },
    { .compatible = "aspeed,ast2600-superio", .data = &ast2600_sioctl_pdata },
    { },
};

static int sioctl_driver_init(struct soc *soc, struct soc_device *dev)
{
    struct sioctl *ctx;
    int rc;

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return -ENOMEM;
    }

    ctx->soc = soc;

    if ((rc = soc_device_get_memory(soc, &dev->node, &ctx->scu)) < 0) {
        goto cleanup_ctx;
    }

    if (!(ctx->pdata = soc_device_get_match_data(soc, sioctl_matches, &dev->node))) {
        loge("Failed to find sioctl platform data\n");
        rc = -EINVAL;
        goto cleanup_ctx;
    }

    if (!(ctx->strap = strap_get(soc))) {
        loge("Failed to acquire strap controller\n");
        rc = -ENODEV;
        goto cleanup_ctx;
    }

    soc_device_set_drvdata(dev, ctx);

    return 0;

cleanup_ctx:
    free(ctx);

    return rc;
}

static void sioctl_driver_destroy(struct soc_device *dev)
{
    free(soc_device_get_drvdata(dev));
}

struct soc_driver sioctl_driver = {
    .name = "sioctl",
    .matches = sioctl_matches,
    .init = sioctl_driver_init,
    .destroy = sioctl_driver_destroy,
};
REGISTER_SOC_DRIVER(sioctl_driver);

struct sioctl *sioctl_get(struct soc *soc)
{
    return soc_driver_get_drvdata(soc, &sioctl_driver);
}
