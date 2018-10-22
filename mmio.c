// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "mb.h"
#include "mmio.h"

#include <stdint.h>

volatile void *mmio_memcpy(volatile void * restrict dst, const volatile void * restrict src, size_t len)
{
    if (((unsigned long)src & 0x3) != ((unsigned long)dst & 0x3)) {
        while (len) {
            *(volatile uint8_t * restrict)dst++ = *(const volatile uint8_t * restrict)src++;
            len--;
        }

        iob();

        return dst;
    }

    while (len && ((unsigned long)src & 0x3) && ((unsigned long)dst & 0x3)) {
        *(volatile uint8_t * restrict)dst++ = *(const volatile uint8_t * restrict)src++;
        len--;
    }

    while (len > 3) {
        const volatile uint32_t * restrict tsrc = src;
        volatile uint32_t * restrict tdst = dst;
        *tdst = *tsrc;
        len -= sizeof(*tdst);
        src += sizeof(*tsrc);
        dst += sizeof(*tdst);
    }

    while (len) {
        *(volatile uint8_t * restrict)dst++ = *(const volatile uint8_t * restrict)src++;
        len--;
    }

    iob();

    return dst;
}
