/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _MMIO_H
#define _MMIO_H

#include <stddef.h>

volatile void *mmio_memcpy(volatile void *dst, const volatile void *src, size_t len);

#endif
