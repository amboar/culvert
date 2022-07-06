// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2022 IBM Corp.

#include "log.h"
#include "soc/sioctl.h"
#include "soc/strap.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#define SCU_HW_STRAP			0x070
#define   SCU_HW_STRAP_SIO_DEC	        (1 << 20)
#define   SCU_HW_STRAP_SIO_4E           (1 << 16)

#include "soc/strap.h"

struct sioctl {
    struct soc *soc;
    struct soc_region scu;
    struct strap *strap;
};

int sioctl_decode_configure(struct sioctl *ctx, const enum sioctl_decode mode)
{
    int rc;

    if (mode == sioctl_decode_disable) {
        return strap_clear(ctx->strap, SCU_HW_STRAP, SCU_HW_STRAP_SIO_DEC, SCU_HW_STRAP_SIO_DEC);
    }

    if (mode == sioctl_decode_4e) {
        rc = strap_set(ctx->strap, SCU_HW_STRAP, SCU_HW_STRAP_SIO_4E, SCU_HW_STRAP_SIO_4E);
        if (rc < 0) {
            return rc;
        }
    } else {
        assert(mode == sioctl_decode_2e);
        rc = strap_clear(ctx->strap, SCU_HW_STRAP, SCU_HW_STRAP_SIO_4E, SCU_HW_STRAP_SIO_4E);
        if (rc < 0) {
            return rc;
        }
    }

    return strap_set(ctx->strap, SCU_HW_STRAP, SCU_HW_STRAP_SIO_DEC, SCU_HW_STRAP_SIO_DEC);
}

int sioctl_decode_status(struct sioctl *ctx, enum sioctl_decode *status)
{
    uint32_t strap;
    int rc;

    if ((rc = strap_read(ctx->strap, SCU_HW_STRAP, &strap)) < 0) {
        return rc;
    }

    if (!(strap & SCU_HW_STRAP_SIO_DEC)) {
        *status = sioctl_decode_disable;

        return 0;
    }

    *status = (strap & SCU_HW_STRAP_SIO_4E) ? sioctl_decode_4e : sioctl_decode_2e;

    return 0;
}

static int sioctl_driver_init(struct soc *soc, struct soc_device *dev)
{
    struct sioctl *ctx;
    int rc;

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return -ENOMEM;
    }

    if ((rc = soc_device_get_memory(soc, &dev->node, &ctx->scu)) < 0) {
        goto cleanup_ctx;
    }

    ctx->strap = strap_get(soc);
    if (!ctx->strap) {
        loge("Failed to acquire strap controller\n");
        return -ENODEV;
    }

    ctx->soc = soc;

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

static const struct soc_device_id sioctl_matches[] = {
    { .compatible = "aspeed,ast2400-superio" },
    { .compatible = "aspeed,ast2500-superio" },
    { },
};

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
