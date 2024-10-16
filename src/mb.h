/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _MB_H
#define _MB_H

#if defined(__PPC64__)
#define iob() asm volatile("eieio" ::: "memory");
#elif defined(__x86_64__)
#define iob() asm volatile("mfence" ::: "memory");
#elif (defined(__aarch64__) || defined(__arm__)) && defined(__ARM_ARCH)
#if __ARM_ARCH >= 7
#define iob() asm volatile("dsb osh" ::: "memory")
#endif
#endif

#ifndef iob
#define iob()
#endif

#endif
