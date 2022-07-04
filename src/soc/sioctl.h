/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 IBM Corp. */

#ifndef _SOC_SIOCTL_H
#define _SOC_SIOCTL_H

#include "soc.h"

struct sioctl;

enum sioctl_decode { sioctl_decode_disable, sioctl_decode_2e, sioctl_decode_4e };

int sioctl_decode_configure(struct sioctl *ctx, const enum sioctl_decode mode);
int sioctl_decode_status(struct sioctl *ctx, enum sioctl_decode *status);

struct sioctl *sioctl_get(struct soc *soc);

#endif
