// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "array.h"
#include "log.h"
#include "rev.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>

struct bmc_silicon_rev {
    uint32_t rev;
    const char *name;
};

static const struct bmc_silicon_rev bmc_silicon_revs[] = {
    { 0x02000303, "AST2400 A0" },
    { 0x02010303, "AST2400 A1" },
    { 0x04000303, "AST2500 A0" },
    { 0x04010303, "AST2500 A1" },
    { 0x04030303, "AST2500 A2" },
    { 0x05000303, "AST2600 A0" },
    { 0x05010303, "AST2600 A1" },
    { 0x05020303, "AST2600 A2" },
    { 0x05030303, "AST2600 A3" },
};

int64_t rev_probe(struct ahb *ahb)
{
    uint32_t probe[2], rev;
    bool is_g6;
    int rc;
    size_t i;

    logd("Probing for SoC revision registers\n");

    /*
     * The layout of the AST2600 SCU is drastically different from the 2400 and
     * 2500 to the point that the silicon revision registers have moved. We
     * need to do some fingerprinting to work around it.
     *
     * That said, 0x1e6e2000 is the SCU on all of the 2400, 2500 and 2600.
     */
#define AST_SCU 0x1e6e2000

    /*
     * SCU004 is:
     *
     * - AST2400: System Reset Control Register
     * - AST2500: System Reset Control Register
     * - AST2600: Silicon Revision ID Register
     *
     * With the following properties:
     *
     * - AST2400:
     *      [31:28]: Reserved, 0b1111
     *      [27:26]: Reserved, 0b11
     *
     * - AST2500:
     *      [31:26]: Reserved, 0b111111
     *
     * - AST2600:
     *      [31:24]: Reserved, 0x05
     *
     * The AST2600 reserved value of 0x05 = 0b00000101. Given Aspeed have used
     * 0x05 for the AST2600 we can assume they're not using bit positions to
     * indicate the SoC generation. This suggests it should be safe for a while
     * to use the top nibble to sense whether we're looking at the Silicon
     * Revision ID Register of the 2600 or the System Reset Control Register of
     * the 2400 and 2500.
     */
    rc = ahb_readl(ahb, AST_SCU | 0x004, &probe[0]);
    if (rc < 0) { return rc; }
    logt("0x%08" PRIx32 ": 0x%08" PRIx32 "\n", AST_SCU | 0x004, probe[0]);

    /*
     * SCU07C is:
     *
     * - AST2400: Silicon Revision ID Register
     * - AST2500: Silicon Revision ID Register
     * - AST2600: System Reset Event Log Register Set 2-3
     *
     * With the following properties:
     *
     * - AST2400:
     *      [31:24]: Reserved, 0x02
     *
     * - AST2500:
     *      [31:24]: Reserved, 0x04
     *
     * - AST2600:
     *      [31:16]: Reserved, 0x0000
     *
     * The AST2600 reserved value of 0x0000 allows us to infer that any set
     * bits in the top byte indicate that we are _not_ looking at a 2600 SoC.
     */
    rc = ahb_readl(ahb, AST_SCU | 0x07c, &probe[1]);
    if (rc < 0) { return rc; }
    logt("0x%08" PRIx32 ": 0x%08" PRIx32 "\n", AST_SCU | 0x07c, probe[1]);

    is_g6 = !(((probe[0] >> 28) & 0xf) && ((probe[1] >> 24) & 0xff));

    /* Based on the above observations, extract the true silicon revision ID */
    if (is_g6) {
        /*
         * AST2600 A2+ doesn't reflect the stepping in SCU004... that's only
         * indicated in SCU014. But we can't just use 014 across the board
         * because it can't be used to distinguish older chips
         * (AST2400/AST2500) as easily as SCU004. So use it once we know it's
         * the AST2600.
         */
        rc = ahb_readl(ahb, AST_SCU | 0x014, &rev);
        if (rc < 0) { return rc; }
        logt("0x%08" PRIx32 ": 0x%08" PRIx32 "\n", AST_SCU | 0x014, rev);
    } else {
        rev = probe[1];
    }

#undef AST_SCU

    logd("Found revision 0x%x\n", rev);

    /* Is it a supported revision? */
    for (i = 0; i < ARRAY_SIZE(bmc_silicon_revs); i++) {
        if (rev == bmc_silicon_revs[i].rev)
            return rev;
    }

    logd("Revision 0x%x is unsupported\n", rev);

    return -ENODEV;
}

bool rev_is_supported(uint32_t rev)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(bmc_silicon_revs); i++) {
        if (rev == bmc_silicon_revs[i].rev)
            return true;
    }

    return false;
}

const char *rev_name(uint32_t rev) {
    size_t i;

    for (i = 0; i < ARRAY_SIZE(bmc_silicon_revs); i++) {
        if (rev == bmc_silicon_revs[i].rev)
            return bmc_silicon_revs[i].name;
    }

    return NULL;
}

static const uint8_t bmc_silicon_gens[] = {
    [ast_g4] = 0x02,
    [ast_g5] = 0x04,
    [ast_g6] = 0x05,
};

bool rev_is_generation(uint32_t rev, enum ast_generation gen)
{
    assert(gen < ARRAY_SIZE(bmc_silicon_gens));

    return rev >> 24 == bmc_silicon_gens[gen];
}

int rev_stepping(uint32_t rev)
{
    return (rev >> 16) & 0xf;
}
