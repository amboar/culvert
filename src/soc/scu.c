// SPDX-License-Identifier: Apache-2.0

#include <errno.h>

#include "scu.h"

struct scu {
	int refcnt;
	struct soc *soc;
	struct soc_region regs;
};

int scu_readl(struct scu *ctx, uint32_t reg, uint32_t *value)
{
	return soc_readl(ctx->soc, ctx->regs.start + reg, value);
}

int scu_writel(struct scu *ctx, uint32_t reg, uint32_t value)
{
	return soc_writel(ctx->soc, ctx->regs.start + reg, value);
}

static int scu_driver_init(struct soc *soc, struct soc_device *dev)
{
	struct scu *ctx;
	int rc;

	ctx = malloc(sizeof(*ctx));
	if (!ctx) {
		return -ENOMEM;
	}

	if ((rc = soc_device_get_memory(soc, &dev->node, &ctx->regs)) < 0) {
		goto cleanup_ctx;
	}
	ctx->refcnt = 1;
	ctx->soc = soc;

	soc_device_set_drvdata(dev, ctx);

	return 0;

cleanup_ctx:
	free(ctx);
	return rc;
}

static void scu_driver_destroy(struct soc_device *dev)
{
	scu_put(soc_device_get_drvdata(dev));
}

static const struct soc_device_id scu_match[] = {
	{ .compatible = "aspeed,ast2500-scu" },
	{ },
};

static const struct soc_driver scu_driver = {
	.name = "scu",
	.matches = scu_match,
	.init = scu_driver_init,
	.destroy = scu_driver_destroy,
};
REGISTER_SOC_DRIVER(scu_driver);

struct scu *scu_get(struct soc *soc)
{
	struct scu *ctx = soc_driver_get_drvdata(soc, &scu_driver);
	if (ctx) {
		ctx->refcnt += 1;
	}
	return ctx;
}

void scu_put(struct scu *ctx)
{
	ctx->refcnt -= 1;
	if (!ctx->refcnt) {
		free(ctx);
	}
}
