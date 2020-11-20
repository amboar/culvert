/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2020 IBM Corp. */

#ifndef _OTP_H
#define _OTP_H

struct ahb;

enum otp_region {
    otp_region_strap,
    otp_region_conf,
};

struct otp {
    struct ahb *ahb;
    uint32_t soak_parameters[3][3];
};

int otp_init(struct otp *otp, struct ahb *ahb);

int otp_read(struct otp *otp, enum otp_region reg);
int otp_write_conf(struct otp *otp, unsigned int word, unsigned int bit);
int otp_write_strap(struct otp *otp, unsigned int bit, unsigned int val);

#endif
