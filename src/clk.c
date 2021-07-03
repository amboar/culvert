// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "compiler.h"

#include "ast.h"
#include "clk.h"

#include <assert.h>
#include <stdint.h>

int clk_disable(struct ahb *ahb, enum clk_gate gate __unused)
{
    assert(gate == clk_arm);
    return ahb_writel(ahb, AST_G5_SCU | SCU_HW_STRAP, SCU_HW_STRAP_ARM_CLK);
}

int clk_enable(struct ahb *ahb, enum clk_gate gate __unused)
{
    assert(gate == clk_arm);
    return ahb_writel(ahb, AST_G5_SCU | SCU_SILICON_REVISION,
                      SCU_HW_STRAP_ARM_CLK);
}
