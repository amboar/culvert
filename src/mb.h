/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _MB_H
#define _MB_H

#if defined(__PPC64__)
#include "ppc.h"
#define iob() eieio()
#elif defined(__x86_64__)
#include "x86.h"
#define iob() mfence()
#elif defined(__aarch64__)
#define iob() asm volatile("dsb osh" ::: "memory")
#elif defined(__arm__)
/*
 * HACK: Assumes we're running remotely or on the AST itself. If ARM is
 * the host arch then we need to fix up the barriers
 */
#define iob()
#endif

#endif
