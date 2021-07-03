// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "ast.h"
#include "vuart.h"

#include <errno.h>

int vuart_set_host_tx_discard(struct ahb *ahb, enum vuart_discard state)
{
    uint32_t val;
    int rc;

    if (!(state == discard_enable || state == discard_disable))
        return -EINVAL;

    rc = ahb_readl(ahb, AST_G5_VUART | VUART_GCRA, &val);
    if (rc < 0)
        return rc;

    if (state == discard_enable)
        val &= ~VUART_GCRA_TX_DISCARD;
    else
        val |= VUART_GCRA_TX_DISCARD;

    return ahb_writel(ahb, AST_G5_VUART | VUART_GCRA, val);
}
