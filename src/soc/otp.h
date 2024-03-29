/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2020 IBM Corp. */

#ifndef _OTP_H
#define _OTP_H

#include "soc.h"

enum otp_region {
    otp_region_strap,
    otp_region_conf,
};

struct otp;

int otp_read(struct otp *otp, enum otp_region reg);
int otp_write_conf(struct otp *otp, unsigned int word, unsigned int bit);
int otp_write_strap(struct otp *otp, unsigned int bit, unsigned int val);

struct otp *otp_get(struct soc *soc);

#endif
