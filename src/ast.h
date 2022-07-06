/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _AST_H
#define _AST_H

#include "ahb.h"

int ast_ahb_access(const char *name, int argc, char *argv[], struct ahb *ahb);

#endif
