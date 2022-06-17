// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021, Oracle and/or its affiliates.

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "ahb.h"
#include "ast.h"
#include "bits.h"
#include "log.h"
#include "soc.h"
#include "trace.h"

#define AHBC_BUF_LEN                    (64 * 1024)

#define R_AHBC_BCR_CSR	                0x40
#define   AHBC_BCR_CSR_BUF_LEN_SHIFT	8
#define   AHBC_BCR_CSR_BUF_LEN_MASK	GENMASK(10, 8)
#define   AHBC_BCR_CSR_BUF_LEN_4K	0b000
#define   AHBC_BCR_CSR_BUF_LEN_8K	0b001
#define   AHBC_BCR_CSR_BUF_LEN_16K	0b010
#define   AHBC_BCR_CSR_BUF_LEN_32K	0b011
#define   AHBC_BCR_CSR_BUF_LEN_128K	0b100
#define   AHBC_BCR_CSR_BUF_LEN_256K	0b101
#define   AHBC_BCR_CSR_BUF_LEN_512K	0b110
#define   AHBC_BCR_CSR_BUF_LEN_1024K	0b111

#define   AHBC_BCR_CSR_POLL_DATA_SHIFT	4
#define   AHBC_BCR_CSR_POLL_DATA_MASK	GENMASK(6, 4)
#define   AHBC_BCR_CSR_POLL_DATA_1_0	0b000
#define   AHBC_BCR_CSR_POLL_DATA_1_1	0b001
#define   AHBC_BCR_CSR_POLL_DATA_1_2	0b010
#define   AHBC_BCR_CSR_POLL_DATA_1_3	0b011
#define   AHBC_BCR_CSR_POLL_DATA_2_0	0b100
#define   AHBC_BCR_CSR_POLL_DATA_2_2	0b101
#define   AHBC_BCR_CSR_POLL_DATA_4_0	0b110

#define   AHBC_BCR_CSR_FLUSH            BIT(2)
#define   AHBC_BCR_CSR_POLL_MODE        BIT(1)

#define   AHBC_BCR_CSR_POLL_EN          BIT(0)

#define R_AHBC_BCR_BUF                  0x44
#define   AHBC_BCR_BUF_WRAP             BIT(0)
#define R_AHBC_BCR_ADDR                 0x48
#define R_AHBC_BCR_FIFO_MERGE           0x5C

static const size_t ahbc_bcr_buf_len[] = {
    [AHBC_BCR_CSR_BUF_LEN_4K] = (4 * 1024),
    [AHBC_BCR_CSR_BUF_LEN_8K] = (8 * 1024),
    [AHBC_BCR_CSR_BUF_LEN_16K] = (16 * 1024),
    [AHBC_BCR_CSR_BUF_LEN_32K] = (32 * 1024),
    [AHBC_BCR_CSR_BUF_LEN_128K] = (128 * 1024),
    [AHBC_BCR_CSR_BUF_LEN_256K] = (256 * 1024),
    [AHBC_BCR_CSR_BUF_LEN_512K] = (512 * 1024),
    [AHBC_BCR_CSR_BUF_LEN_1024K] = (1024 * 1024),
};

static int trace_style(int width, int offset)
{
    if (!(width == 1 || width == 2 || width == 4))
        return -EINVAL;

    if (offset >= 4 || (offset & (width - 1)))
        return -EINVAL;

    switch (width) {
    case 1:
        switch (offset) {
        case 0:
            return AHBC_BCR_CSR_POLL_DATA_1_0;

        case 1:
            return AHBC_BCR_CSR_POLL_DATA_1_1;

        case 2:
            return AHBC_BCR_CSR_POLL_DATA_1_2;

        case 3:
            return AHBC_BCR_CSR_POLL_DATA_1_3;

        default:
            assert(false);
        }
        break;

    case 2:
        assert(offset == 0 || offset == 2);

        return offset == 0
            ? AHBC_BCR_CSR_POLL_DATA_2_0
            : AHBC_BCR_CSR_POLL_DATA_2_2;

    case 4:
        assert(offset == 0);

        return AHBC_BCR_CSR_POLL_DATA_4_0;
    }

    assert(false);

    return -EINVAL;
}

static int ahbc_readl(struct trace *ctx, uint32_t off, uint32_t *val)
{
    return soc_readl(ctx->soc, ctx->ahbc.start + off, val);
}

static int ahbc_writel(struct trace *ctx, uint32_t off, uint32_t val)
{
    return soc_writel(ctx->soc, ctx->ahbc.start + off, val);
}

static const struct soc_device_id ahbc_match[] = {
    { .compatible = "aspeed,ast2500-ahb-controller" },
    { .compatible = "aspeed,ast2600-ahb-controller" },
    { },
};

int trace_init(struct trace *ctx, struct soc *soc)
{
    struct soc_device_node dn;
    int rc;

    if ((rc = soc_device_match_node(soc, ahbc_match, &dn)) < 0)
        return rc;

    if ((rc = soc_device_get_memory(soc, &dn, &ctx->ahbc)) < 0)
        return rc;

    if ((rc = soc_device_get_memory_region_named(soc, &dn, "trace-buffer", &ctx->sram)) < 0)
        return rc;

    logi("Found AHBC at 0x%" PRIx32 " and SRAM at 0x%" PRIx32 "\n",
         ctx->ahbc.start, ctx->sram.start);

    ctx->soc = soc;

    return 0;
}

int trace_start(struct trace *ctx, uint32_t addr, int width, enum trace_mode mode)
{
    uint32_t csr, buf;
    size_t i;
    int rc;

    logd("%s: 0x%08" PRIx32 " %d %d\n", __func__, addr, width, mode);

    assert(ctx->sram.length >= (32 * 1024));
    csr = AHBC_BCR_CSR_BUF_LEN_32K << AHBC_BCR_CSR_BUF_LEN_SHIFT;
    csr |= AHBC_BCR_CSR_POLL_MODE * mode;

    if ((rc = ahbc_writel(ctx, R_AHBC_BCR_CSR, csr)))
        return rc;

    if ((rc = ahbc_writel(ctx, R_AHBC_BCR_ADDR, addr & ~3)))
        return rc;

    logi("Zeroing trace buffer [%p - %p]\n", ctx->sram.start, ctx->sram.start + ctx->sram.length);

    for (i = 0; i < (ctx->sram.length / 4); i++) {
        soc_writel(ctx->soc, 4 * i + ctx->sram.start, 0);
    }

    buf = ctx->sram.start | AHBC_BCR_BUF_WRAP;
    if ((rc = ahbc_writel(ctx, R_AHBC_BCR_BUF, buf)))
        return rc;

    if ((rc = trace_style(width, addr & 3)) < 0)
        return rc;

    csr |= rc << AHBC_BCR_CSR_POLL_DATA_SHIFT;
    csr |= AHBC_BCR_CSR_FLUSH;
    csr |= AHBC_BCR_CSR_POLL_EN;

    if ((rc = ahbc_writel(ctx, R_AHBC_BCR_CSR, csr)))
        return rc;

    logi("Started AHB trace for 0x%08" PRIx32 "\n", addr);

    return 0;
}

int trace_stop(struct trace *ctx)
{
    uint32_t csr;
    int rc;

    if ((rc = ahbc_readl(ctx, R_AHBC_BCR_CSR, &csr)))
        return rc;

    if (!(csr & AHBC_BCR_CSR_POLL_EN)) {
        /* Tracing isn't running, we're done */
        return 0;
    }

    logt("%s: csr: 0x%08" PRIx32 "\n", __func__, csr);

    /* Note: This won't flush the tail values if they don't form a full word */
    csr |= AHBC_BCR_CSR_FLUSH;
    if ((rc = ahbc_writel(ctx, R_AHBC_BCR_CSR, csr)))
        return rc;

    csr &= ~(AHBC_BCR_CSR_POLL_EN | AHBC_BCR_CSR_FLUSH);

    if ((rc = ahbc_writel(ctx, R_AHBC_BCR_CSR, csr)))
        return rc;

    logi("Stopped AHB trace\n");

    return 0;
}

int trace_dump(struct trace *ctx, int outfd)
{
    uint32_t buf_len, write_ptr;
    uint32_t csr, buf;
    uint32_t merge;
    uint32_t base;
    bool wrapped;
    ssize_t rc;
    size_t len;

    if ((rc = ahbc_readl(ctx, R_AHBC_BCR_CSR, &csr)))
        return rc;

    logt("%s: csr: 0x%08" PRIx32 "\n", __func__, csr);

    if ((rc = ahbc_readl(ctx, R_AHBC_BCR_BUF, &buf)))
        return rc;

    logt("%s: buf: 0x%08" PRIx32 "\n", __func__, buf);

    /*
     * 1 and 2 byte trace entries are accumulated in the merge FIFO. Once the
     * merge FIFO has 4 bytes of data it's moved into the "real" FIFO regs and
     * eventually flushed to the trace buffer. If you're tracing byte accesses
     * you might not see anything flushed to the trace buffer, but it'll be in
     * the merge FIFO.
     */
    rc = ahbc_readl(ctx, R_AHBC_BCR_FIFO_MERGE, &merge);
    if (!rc)
        logi("%s: partial trace reg: 0x%08" PRIx32 "\n", __func__, merge);

    wrapped = buf & AHBC_BCR_BUF_WRAP;
    buf &= ~AHBC_BCR_BUF_WRAP;

    buf_len = (csr & AHBC_BCR_CSR_BUF_LEN_MASK) >> AHBC_BCR_CSR_BUF_LEN_SHIFT;
    write_ptr = buf & GENMASK((11 + buf_len), 2);
    base = (buf & ~(write_ptr | 3));

    if (wrapped) {
        len = (base + ahbc_bcr_buf_len[buf_len]) - write_ptr;

        logd("Ring buffer has wrapped, dumping trace buffer from write pointer at 0x%" PRIx32 " for %zu\n",
             buf, len);
        if ((rc = soc_siphon_in(ctx->soc, buf, len, outfd)))
            return rc;
    }

    len = buf - base;

    logd("Dumping from trace buffer at 0x%" PRIx32 " for %zu\n", base, len);

    if ((rc = soc_siphon_in(ctx->soc, base, len, outfd)))
        return rc;

    return rc;
}
