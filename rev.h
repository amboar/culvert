/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _REV_H
#define _REV_H

#include "ast.h"
#include "ahb.h"

#include <stdbool.h>
#include <stdint.h>

int64_t rev_probe(struct ahb *ahb);
bool rev_is_supported(uint32_t rev);
bool rev_is_generation(uint32_t rev, enum ast_generation gen);
const char *rev_name(uint32_t rev);

#endif
