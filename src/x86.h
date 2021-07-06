/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _X86_H
#define _X86_H

/* Heavy-handed, at some point we might care about performance */
static inline void mfence(void)
{
	asm volatile("mfence" : : : "memory");
}

#endif
