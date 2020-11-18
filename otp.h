/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2020 IBM Corp. */

#ifndef _OTP_H
#define _OTP_H

struct ahb;

enum otp_region {
    otp_region_strap,
    otp_region_conf,
};

int otp_read(struct ahb *ahb, enum otp_region reg);
int otp_write_conf(struct ahb *ahb, unsigned int word, unsigned int bit);
int otp_write_strap(struct ahb *ahb, unsigned int bit, unsigned int val);

#endif
