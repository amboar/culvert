/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _BITS_H
#define _BITS_H

#define BIT(x)		(1UL << (x))
#define GENMASK(h, l)	((BIT(h) | (BIT(h) - 1)) & ~(BIT(l) - 1))

#endif
