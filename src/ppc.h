/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _PPC_H
#define _PPC_H

static inline void eieio(void)
{
    asm volatile("eieio" : : : "memory");
}

#endif
