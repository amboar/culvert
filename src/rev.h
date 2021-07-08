/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _REV_H
#define _REV_H

#include "ahb.h"

#include <stdbool.h>
#include <stdint.h>

enum ast_generation { ast_g4, ast_g5, ast_g6 };

int64_t rev_probe(struct ahb *ahb);
bool rev_is_supported(uint32_t rev);
enum ast_generation rev_generation(uint32_t rev);
bool rev_is_generation(uint32_t rev, enum ast_generation gen);
const char *rev_name(uint32_t rev);
int rev_stepping(uint32_t rev);

#endif
