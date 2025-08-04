/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _AST_H
#define _AST_H

#include "ahb.h"

#include <stdbool.h>
#include <stdlib.h>

struct ast_ahb_args {
    /**
     * read_length defines the width of an address to be read and is
     * not always required.
     */
    uint32_t *read_length;

    /**
     * write_value is only required for bit-wise operations like register
     * modifications. For binary data, please consider to pipe the data to stdin.
     */
    uint32_t *write_value;
    uint32_t address;
    bool read;
};

int ast_ahb_access(struct ast_ahb_args *args, struct ahb *ahb);

#endif
