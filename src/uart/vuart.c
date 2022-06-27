// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "log.h"
#include "vuart.h"

#include <errno.h>

#define   VUART_GCRA                    0x20
#define     VUART_GCRA_TX_DISCARD       (1 << 5)

struct vuart {
	struct soc *soc;
	struct soc_region iomem;
};

static const struct soc_device_id vuart_match[] = {
    { .compatible = "aspeed,ast2400-vuart" },
    { .compatible = "aspeed,ast2500-vuart" },
    { .compatible = "aspeed,ast2600-vuart" },
    { },
};

int vuart_init(struct vuart *ctx, struct soc *soc, const char *name)
{
    struct soc_device_node dn;
    int rc;

    rc = soc_device_from_name(soc, name, &dn);
    if (rc < 0) {
        loge("vuart: Failed to find device by name '%s': %d\n", name, rc);
        return rc;
    }

    rc = soc_device_is_compatible(soc, vuart_match, &dn);
    if (rc < 0) {
        loge("vuart: Failed verify device compatibility: %d\n", rc);
        return rc;
    }

    if (!rc) {
        loge("vuart: Incompatible device described by node '%s'\n", name);
        return -EINVAL;
    }

    if ((rc = soc_device_get_memory(soc, &dn, &ctx->iomem)) < 0)
        return rc;

    ctx->soc = soc;

    return 0;
}

int vuart_set_host_tx_discard(struct vuart *ctx, enum vuart_discard state)
{
    uint32_t val;
    int rc;

    if (!(state == discard_enable || state == discard_disable))
        return -EINVAL;

    rc = soc_readl(ctx->soc, ctx->iomem.start + VUART_GCRA, &val);
    if (rc < 0)
        return rc;

    if (state == discard_enable)
        val &= ~VUART_GCRA_TX_DISCARD;
    else
        val |= VUART_GCRA_TX_DISCARD;

    return soc_writel(ctx->soc, ctx->iomem.start + VUART_GCRA, val);
}

void vuart_destroy(struct vuart *ctx)
{
    ctx->soc = NULL;
}

static int vuart_driver_init(struct soc *soc, struct soc_device *dev)
{
    struct vuart *ctx;
    int rc;

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return -ENOMEM;
    }

    if ((rc = vuart_init(ctx, soc, "vuart")) < 0) {
        goto cleanup_ctx;
    }

    soc_device_set_drvdata(dev, ctx);

    return 0;

cleanup_ctx:
    free(ctx);

    return rc;
}

static void vuart_driver_destroy(struct soc_device *dev)
{
    struct vuart *ctx = soc_device_get_drvdata(dev);

    vuart_destroy(ctx);

    free(ctx);
}

static const struct soc_driver vuart_driver = {
    .name = "vuart",
    .matches = vuart_match,
    .init = vuart_driver_init,
    .destroy = vuart_driver_destroy,
};
REGISTER_SOC_DRIVER(&vuart_driver);

struct vuart *vuart_get_by_name(struct soc *soc, const char *name)
{
    return soc_driver_get_drvdata_by_name(soc, &vuart_driver, name);
}
