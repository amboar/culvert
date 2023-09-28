// SPDX-License-Identifier: Apache-2.0

#include <errno.h>

#include "scu.h"

#define AST_SCU_PROT_KEY	0x000
#define AST_SCU_PASSWORD	0x1688a8a8

struct scu {
	int refcnt;
	bool was_locked;
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

static int scu_is_locked(struct scu *ctx, bool *locked)
{
	uint32_t value;
	int rc = scu_readl(ctx, AST_SCU_PROT_KEY, &value);
	if (rc) {
		return rc;
	}
	*locked = !value;
	return 0;
}

static int scu_unlock(struct scu *ctx)
{
	return scu_writel(ctx, AST_SCU_PROT_KEY, AST_SCU_PASSWORD);
}

static int scu_lock(struct scu *ctx)
{
	return scu_writel(ctx, AST_SCU_PROT_KEY, ~AST_SCU_PASSWORD);
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

	if ((rc = scu_is_locked(ctx, &ctx->was_locked)) < 0) {
		goto cleanup_ctx;
	}

	if (ctx->was_locked) {
		logd("Unlocking SCU\n");
		if ((rc = scu_unlock(ctx)) < 0) {
			goto cleanup_ctx;
		}
	}

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
	{ .compatible = "aspeed,ast2400-scu" },
	{ .compatible = "aspeed,ast2500-scu" },
	{ .compatible = "aspeed,ast2600-scu" },
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
		if (ctx->was_locked) {
			logd("Re-locking SCU\n");
			if (scu_lock(ctx)) {
				loge("Failed to re-lock SCU\n");
			}
		}
		free(ctx);
	}
}
