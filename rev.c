// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "array.h"
#include "rev.h"

#include <assert.h>
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
};

bool rev_is_supported(uint32_t rev) {
    int i;

    for (i = 0; i < ARRAY_SIZE(bmc_silicon_revs); i++) {
        if (rev == bmc_silicon_revs[i].rev)
            return true;
    }

    return false;
}

const char *rev_name(uint32_t rev) {
    int i;

    for (i = 0; i < ARRAY_SIZE(bmc_silicon_revs); i++) {
        if (rev == bmc_silicon_revs[i].rev)
            return bmc_silicon_revs[i].name;
    }

    return NULL;
}

static const uint8_t bmc_silicon_gens[] = {
    [ast_g4] = 0x02,
    [ast_g5] = 0x04,
};

bool rev_is_generation(uint32_t rev, enum ast_generation gen)
{
    assert(gen < ARRAY_SIZE(bmc_silicon_gens));

    return rev >> 24 == bmc_silicon_gens[gen];
}
