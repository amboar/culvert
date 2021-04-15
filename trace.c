#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "ahb.h"
#include "ast.h"
#include "bits.h"
#include "log.h"
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

#define TRACE_BUF_BASE  AST_G6_SRAM
#define TRACE_BUF_SIZE  (32 * 1024)

static int ahbc_readl(struct ahb *ahb, uint32_t off, uint32_t *val)
{
    return ahb_readl(ahb, AST_G6_AHBC + off, val);
}

static int ahbc_writel(struct ahb *ahb, uint32_t off, uint32_t val)
{
    return ahb_writel(ahb, AST_G6_AHBC + off, val);
}

int trace_start(struct ahb *ahb, uint32_t addr, int width, int offset,
                enum trace_mode mode)
{
    uint32_t csr, buf;
    int rc;

    logd("%s: 0x%08" PRIx32 " %d:%d %d\n", __func__, addr, width, offset, mode);

    assert(TRACE_BUF_SIZE == (32 * 1024));
    csr = AHBC_BCR_CSR_BUF_LEN_32K << AHBC_BCR_CSR_BUF_LEN_SHIFT;
    csr |= AHBC_BCR_CSR_POLL_MODE * mode;

    if ((rc = ahbc_writel(ahb, R_AHBC_BCR_CSR, csr)))
        return rc;

    if ((rc = ahbc_writel(ahb, R_AHBC_BCR_ADDR, addr)))
        return rc;

    for (int i = 0; i < (TRACE_BUF_SIZE / 4); i++) {
        ahb_writel(ahb, addr, 0);
    }

    buf = TRACE_BUF_BASE | AHBC_BCR_BUF_WRAP;
    if ((rc = ahbc_writel(ahb, R_AHBC_BCR_BUF, buf)))
        return rc;

    if ((rc = trace_style(width, offset)) < 0)
        return rc;

    csr |= rc << AHBC_BCR_CSR_POLL_DATA_SHIFT;
    csr |= AHBC_BCR_CSR_FLUSH;
    csr |= AHBC_BCR_CSR_POLL_EN;

    if ((rc = ahbc_writel(ahb, R_AHBC_BCR_CSR, csr)))
        return rc;

    logi("Started AHB trace for 0x%08" PRIx32 "\n", addr);

    return 0;
}

int trace_stop(struct ahb *ahb)
{
    uint32_t csr;
    int rc;

    if ((rc = ahbc_readl(ahb, R_AHBC_BCR_CSR, &csr)))
        return rc;

    logt("%s: csr: 0x%08" PRIx32 "\n", __func__, csr);

    /* Note: This won't flush the tail values if they don't form a full word */
    csr |= AHBC_BCR_CSR_FLUSH;
    if ((rc = ahbc_writel(ahb, R_AHBC_BCR_CSR, csr)))
        return rc;

    csr &= ~(AHBC_BCR_CSR_POLL_EN | AHBC_BCR_CSR_FLUSH);

    if ((rc = ahbc_writel(ahb, R_AHBC_BCR_CSR, csr)))
        return rc;

    logi("Stopped AHB trace\n");

    return 0;
}

int trace_dump(struct ahb *ahb, int outfd)
{
    uint32_t buf_len, write_ptr;
    uint32_t csr, buf;
    uint32_t base;
    bool wrapped;
    ssize_t rc;
    size_t len;

    if ((rc = ahbc_readl(ahb, R_AHBC_BCR_CSR, &csr)))
        return rc;

    logt("%s: csr: 0x%08" PRIx32 "\n", __func__, csr);

    if ((rc = ahbc_readl(ahb, R_AHBC_BCR_BUF, &buf)))
        return rc;

    logt("%s: buf: 0x%08" PRIx32 "\n", __func__, buf);

    wrapped = buf & AHBC_BCR_BUF_WRAP;
    buf &= ~AHBC_BCR_BUF_WRAP;

    buf_len = (csr & AHBC_BCR_CSR_BUF_LEN_MASK) >> AHBC_BCR_CSR_BUF_LEN_SHIFT;
    write_ptr = buf & GENMASK((11 + buf_len), 2);
    base = (buf & ~(write_ptr | 3));

    if (wrapped) {
        len = (base + ahbc_bcr_buf_len[buf_len]) - write_ptr;

        logd("Ring buffer has wrapped, dumping trace buffer from write pointer at 0x%" PRIx32 " for %zu\n",
             buf, len);
        if ((rc = ahb_siphon_in(ahb, buf, len, outfd)))
            return rc;
    }

    len = buf - base;

    logd("Dumping from trace buffer at 0x%" PRIx32 " for %zu\n", base, len);

    if ((rc = ahb_siphon_in(ahb, base, len, outfd)))
        return rc;

    return rc;
}
