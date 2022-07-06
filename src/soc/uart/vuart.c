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

static int vuart_driver_init(struct soc *soc, struct soc_device *dev)
{
    struct vuart *ctx;
    int rc;

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return -ENOMEM;
    }

    if ((rc = soc_device_get_memory(soc, &dev->node, &ctx->iomem)) < 0) {
        goto cleanup_ctx;
    }

    ctx->soc = soc;

    soc_device_set_drvdata(dev, ctx);

    return 0;

cleanup_ctx:
    free(ctx);

    return rc;
}

static void vuart_driver_destroy(struct soc_device *dev)
{
    free(soc_device_get_drvdata(dev));
}

static const struct soc_driver vuart_driver = {
    .name = "vuart",
    .matches = vuart_match,
    .init = vuart_driver_init,
    .destroy = vuart_driver_destroy,
};
REGISTER_SOC_DRIVER(vuart_driver);

struct vuart *vuart_get_by_name(struct soc *soc, const char *name)
{
    return soc_driver_get_drvdata_by_name(soc, &vuart_driver, name);
}
