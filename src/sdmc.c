// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021 IBM Corp.

#include "bits.h"
#include "sdmc.h"

#include <errno.h>

#define MCR_CONFIG      0x04
#define MCR_GMP         0x08

static const uint32_t ast_vram_sizes[4] = {
    [0b00] = 8  << 20,
    [0b01] = 16 << 20,
    [0b10] = 32 << 20,
    [0b11] = 64 << 20,
};

static const uint32_t ast2400_dram_sizes[4] = {
    [0b00] = 64  << 20,
    [0b01] = 128 << 20,
    [0b10] = 256 << 20,
    [0b11] = 512 << 20,
};

static const struct sdmc_pdata ast2400_sdmc_pdata = {
    .dram_sizes = &ast2400_dram_sizes,
    .vram_sizes = &ast_vram_sizes,
    .gmp_xdma_mask = BIT(16),
};

static const uint32_t ast2500_dram_sizes[4] = {
    [0b00] = 128  << 20,
    [0b01] = 256  << 20,
    [0b10] = 512  << 20,
    [0b11] = 1024 << 20,
};

static const struct sdmc_pdata ast2500_sdmc_pdata = {
    .dram_sizes = &ast2500_dram_sizes,
    .vram_sizes = &ast_vram_sizes,
    .gmp_xdma_mask = BIT(17),
};

static const uint32_t ast2600_dram_sizes[4] = {
    [0b00] = 256  << 20,
    [0b01] = 512  << 20,
    [0b10] = 1024 << 20,
    [0b11] = 2048 << 20,
};

static const struct sdmc_pdata ast2600_sdmc_pdata = {
    .dram_sizes = &ast2600_dram_sizes,
    .vram_sizes = &ast_vram_sizes,
    .gmp_xdma_mask = BIT(18),
};

static const struct soc_device_id sdmc_match[] = {
    {
        .compatible = "aspeed,ast2400-sdram-controller",
        .data = &ast2400_sdmc_pdata
    },
    {
        .compatible = "aspeed,ast2500-sdram-controller",
        .data = &ast2500_sdmc_pdata
    },
    {
        .compatible = "aspeed,ast2600-sdram-controller",
        .data = &ast2600_sdmc_pdata
    },
    { },
};

static int sdmc_readl(struct sdmc *ctx, uint32_t off, uint32_t *val)
{
    return soc_readl(ctx->soc, ctx->iomem.start + off, val);
}

int sdmc_init(struct sdmc *ctx, struct soc *soc)
{
    struct soc_device_node dn;
    int rc;

    if ((rc = soc_device_match_node(soc, sdmc_match, &dn)) < 0)
        return rc;

    if ((rc = soc_device_get_memory(soc, &dn, &ctx->iomem)) < 0)
        return rc;

    if (!(ctx->pdata = soc_device_get_match_data(soc, sdmc_match, &dn)))
        return -EINVAL;

    if ((rc = soc_device_from_type(soc, "memory", &dn)))
        return rc;

    if ((rc = soc_device_get_memory(soc, &dn, &ctx->dram)) < 0)
        return rc;

    ctx->soc = soc;

    return 0;
}

static void sdmc_dram_region(struct sdmc *ctx, uint32_t mcr_conf,
                             struct soc_region *dram)
{
    dram->start = ctx->dram.start;
    dram->length = (*ctx->pdata->dram_sizes)[mcr_conf & 3];
}

int sdmc_get_dram(struct sdmc *ctx, struct soc_region *dram)
{
    uint32_t mcr_conf;
    int rc;

    if ((rc = sdmc_readl(ctx, MCR_CONFIG, &mcr_conf)) < 0)
        return rc;

    sdmc_dram_region(ctx, mcr_conf, dram);

    return 0;
}

int sdmc_get_vram(struct sdmc *ctx, struct soc_region *vram)
{
    struct soc_region dram;
    uint32_t mcr_conf;
    int rc;

    if ((rc = sdmc_readl(ctx, MCR_CONFIG, &mcr_conf)) < 0)
        return rc;

    sdmc_dram_region(ctx, mcr_conf, &dram);

    vram->length = (*ctx->pdata->vram_sizes)[(mcr_conf >> 2) & 3];
    vram->start = dram.start + dram.length - vram->length;

    return 0;
}

int sdmc_constrains_xdma(struct sdmc *ctx)
{
    uint32_t mcr_gmp;
    int rc;

    if ((rc = sdmc_readl(ctx, MCR_GMP, &mcr_gmp)) < 0)
        return rc;

    return !!(mcr_gmp & ctx->pdata->gmp_xdma_mask);
}

void sdmc_destroy(struct sdmc *ctx)
{
    ctx->soc = NULL;
}
