/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _BITS_H
#define _BITS_H

#define BIT(x)		      (1UL << (x))
#define GENMASK(h, l)	      ((BIT(h) | (BIT(h) - 1)) & ~(BIT(l) - 1))
#define FIELD_PREP(mask, val) (((val) << __builtin_ctz(mask)) & (mask))
#define FIELD_GET(mask, val)  (((val) & (mask)) >> __builtin_ctz(mask))

#endif
