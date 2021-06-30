// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2020 IBM Corp.

#define _GNU_SOURCE
//#define _OTP_DEBUG
#include "ahb.h"
#include "log.h"
#include "otp.h"
#include "rev.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define OTP_PASSWD			    0x349fe38a
#define OTP_TRIGGER_PROGRAM     0x23b1e364
#define OTP_TRIGGER_READ        0x23b1e361
#define OTP_TRIGGER_WRITE_REG   0x23b1e362

#define OTP_BASE        0x1e6f2000
#define OTP_PROTECT_KEY OTP_BASE
#define OTP_COMMAND     (OTP_BASE + 0x4)
#define OTP_TIMING      (OTP_BASE + 0x8)
#define OTP_ADDR        (OTP_BASE + 0x10)
#define OTP_STATUS      (OTP_BASE + 0x14)
#define  OTP_STATUS_IDLE 0x6
#define OTP_COMPARE_1   (OTP_BASE + 0x20)
#define OTP_COMPARE_2   (OTP_BASE + 0x24)
#define OTP_COMPARE_3   (OTP_BASE + 0x28)
#define OTP_COMPARE_4   (OTP_BASE + 0x2c)

#define NUM_OTP_CONF    16
#define NUM_PROG_TRIES  16

struct otpstrap_status {
    uint8_t value;
    uint8_t option_array[7];
    uint8_t remain_times;
    uint8_t writeable_option;
    uint8_t protected;
};

#ifdef _OTP_DEBUG
static int otp_readl(struct ahb *ahb, uint32_t phys, uint32_t *val)
{
    int rc = ahb_readl(ahb, phys, val);

    if (rc)
        printf("otp rd %02x failed: %d\n", phys - OTP_BASE, rc);
    else
        printf("otp rd %02x:%08x\n", phys - OTP_BASE, *val);
    return rc;
}
static int otp_writel(struct ahb *ahb, uint32_t phys, uint32_t val)
{
    int rc = ahb_writel(ahb, phys, val);

    if (rc)
        printf("otp wr %02x failed: %d\n", phys - OTP_BASE, rc);
    else
        printf("otp wr %02x:%08x\n", phys - OTP_BASE, val);
    return rc;
}
#else
#define otp_readl ahb_readl
#define otp_writel ahb_writel
#endif

static void diff_timespec(const struct timespec *start,
                          const struct timespec *end, struct timespec *diff)
{
    diff->tv_sec = end->tv_sec - start->tv_sec;
    diff->tv_nsec = end->tv_nsec - start->tv_nsec;
    if (diff->tv_nsec < 0) {
        --diff->tv_sec;
        diff->tv_nsec += 1000000000L;
    }
}

static int otp_wait_complete(struct ahb *ahb)
{
    int rc;
    uint32_t reg = 0;
    struct timespec diff, end, intv, start;

    intv.tv_sec = 0;
    intv.tv_nsec = 1000000L;
    clock_gettime(CLOCK_MONOTONIC, &start);

    do {
        rc = otp_readl(ahb, OTP_STATUS, &reg);
        if (rc)
            return rc;

        if ((reg & OTP_STATUS_IDLE) == OTP_STATUS_IDLE)
            return 0;

        rc = clock_nanosleep(CLOCK_MONOTONIC, 0, &intv, NULL);
        if (rc)
            return rc;

        clock_gettime(CLOCK_MONOTONIC, &end);
        diff_timespec(&start, &end, &diff);
    } while (!diff.tv_sec && diff.tv_nsec < 500000000L);

    return -ETIMEDOUT;
}

static int otp_program(struct ahb *ahb, uint32_t addr, uint32_t val)
{
    int rc = otp_writel(ahb, OTP_ADDR, addr);

    if (rc)
        return rc;

    rc = otp_writel(ahb, OTP_COMPARE_1, val);
    if (rc)
        return rc;

    rc = otp_writel(ahb, OTP_COMMAND, OTP_TRIGGER_PROGRAM);
    if (rc)
        return rc;

    return otp_wait_complete(ahb);
}

static int otp_read_reg(struct ahb *ahb, uint32_t addr, uint32_t *val)
{
    int rc = otp_writel(ahb, OTP_ADDR, addr);

    if (rc)
        return rc;

    rc = otp_writel(ahb, OTP_COMMAND, OTP_TRIGGER_READ);
    if (rc)
        return rc;

    rc = otp_wait_complete(ahb);
    if (rc)
        return rc;

    rc = otp_readl(ahb, OTP_COMPARE_1, val);
    if (rc)
        return rc;

    return 0;
}

static int otp_read_config(struct ahb *ahb, int offset, uint32_t *val)
{
    uint32_t config_offset = 0x800;

    config_offset |= (offset / 8) * 0x200;
    config_offset |= (offset % 8) * 2;

    return otp_read_reg(ahb, config_offset, val);
}

static int otp_write_reg(struct ahb *ahb, uint32_t addr, uint32_t val)
{
    int rc = otp_writel(ahb, OTP_ADDR, addr);

    if (rc)
        return rc;

    rc = otp_writel(ahb, OTP_COMPARE_1, val);
    if (rc)
        return rc;

    rc = otp_writel(ahb, OTP_COMMAND, OTP_TRIGGER_WRITE_REG);
    if (rc)
        return rc;

    return otp_wait_complete(ahb);
}

static int otp_set_soak(struct otp *otp, unsigned int soak)
{
    int rc;

    if (soak > 2)
        return -EINVAL;

    rc = otp_write_reg(otp->ahb, 0x3000, otp->soak_parameters[soak][0]);
    if (rc)
        return rc;

    rc = otp_write_reg(otp->ahb, 0x5000, otp->soak_parameters[soak][1]);
    if (rc)
        return rc;

    rc = otp_write_reg(otp->ahb, 0x1000, otp->soak_parameters[soak][2]);
    if (rc)
        return rc;

    return otp_writel(otp->ahb, OTP_TIMING, otp->timings[soak]);
}

static int otp_confirm()
{
    int rc;
    char inp[8];

    printf("Is this acceptable? If so, type YES: ");
    rc = scanf("%7s", inp);
    if (rc == EOF) {
        loge("Invalid input\n");
        return -errno;
    }

    if (strcmp(inp, "YES")) {
        loge("Strap write unconfirmed\n");
        return -EINVAL;
    }

    return 0;
}

static int otp_write(struct otp *otp, uint32_t address, uint32_t bitmask)
{
    int rc;
    int tries = 0;
    uint32_t prog;
    uint32_t readback;

    rc = otp_set_soak(otp, 1);
    if (rc)
        return rc;

    prog = ~bitmask;
    rc = otp_program(otp->ahb, address, prog);
    if (rc)
        goto undo_soak;

    do {
        rc = otp_read_reg(otp->ahb, address, &readback);
        if (rc)
            goto undo_soak;

        if (readback & bitmask)
            break;

        rc = otp_set_soak(otp, (tries % 2) ? 1 : 2);
        if (rc)
            goto undo_soak;

        rc = otp_program(otp->ahb, address, prog);
        if (rc)
            goto undo_soak;
    } while (tries++ < NUM_PROG_TRIES);

    if (tries == NUM_PROG_TRIES) {
        loge("Failed to program OTP\n");
        rc = -EREMOTEIO;
    } else
        logi("Success!\n");

undo_soak:
    otp_set_soak(otp, 0);

    return rc;
}

int otp_init(struct otp *otp, struct ahb *ahb)
{
    int64_t rev;

    otp->ahb = ahb;

    rev = rev_probe(ahb);
    if (rev < 0)
        return (int)rev;

    if (!rev_is_generation((uint32_t)rev, ast_g6))
        return -EOPNOTSUPP;

    if (rev_stepping((uint32_t)rev) >= 2) {
        logi("Detected AST2600 A2\n");

        otp->timings[0] = 0x04190760;
        otp->timings[1] = 0x04191388;
        otp->timings[2] = 0x04193a98;

        otp->soak_parameters[0][0] = 0x0210;
        otp->soak_parameters[0][1] = 0x2000;
        otp->soak_parameters[0][2] = 0x0;

        otp->soak_parameters[1][0] = 0x1200;
        otp->soak_parameters[1][1] = 0x107f;
        otp->soak_parameters[1][2] = 0x1024;

        otp->soak_parameters[2][0] = 0x1220;
        otp->soak_parameters[2][1] = 0x2074;
        otp->soak_parameters[2][2] = 0x08a4;
    } else {
        logi("Detected AST2600 A0/A1\n");

        otp->timings[0] = 0x04190760;
        otp->timings[1] = 0x04190760;
        otp->timings[2] = 0x041930d4;

        otp->soak_parameters[0][0] = 0x0;
        otp->soak_parameters[0][1] = 0x0;
        otp->soak_parameters[0][2] = 0x0;

        otp->soak_parameters[1][0] = 0x4021;
        otp->soak_parameters[1][1] = 0x302f;
        otp->soak_parameters[1][2] = 0x4020;

        otp->soak_parameters[2][0] = 0x4021;
        otp->soak_parameters[2][1] = 0x1027;
        otp->soak_parameters[2][2] = 0x4820;
    }

    return 0;
}

int otp_read(struct otp *otp, enum otp_region reg)
{
    int rc;

    rc = otp_writel(otp->ahb, OTP_PROTECT_KEY, OTP_PASSWD);
    if (rc)
        return rc;

    if (reg == otp_region_strap) {
        int i;
        uint32_t res[2] = { 0, 0};
        uint32_t strap[6][2];
        uint32_t protect[2];
        uint32_t scu_protect[2];

        rc = otp_read_config(otp->ahb, 28, &scu_protect[0]);
        if (rc)
            goto done;

        rc = otp_read_config(otp->ahb, 29, &scu_protect[1]);
        if (rc)
            goto done;

        rc = otp_read_config(otp->ahb, 30, &protect[0]);
        if (rc)
            goto done;

        rc = otp_read_config(otp->ahb, 31, &protect[1]);
        if (rc)
            goto done;

        for (i = 0; i < 6; ++i) {
            int o = 16 + (i * 2);

            rc = otp_read_config(otp->ahb, o, &strap[i][0]);
            if (rc)
                goto done;

            rc = otp_read_config(otp->ahb, o + 1, &strap[i][1]);
            if (rc)
                    goto done;

            res[0] ^= strap[i][0];
            res[1] ^= strap[i][1];
        }

        logi("OTP straps:\t\t63    32 31     0\n");
        logi("Protect SCU:\t%08x %08x\n", scu_protect[1], scu_protect[0]);
        logi("Protect:\t\t%08x %08x\n", protect[1], protect[0]);

        for (i = 0; i < 6; ++i)
            logi("Option %u:\t\t%08x %08x\n", i, strap[i][1], strap[i][0]);

        logi("Result:\t\t%08x %08x\n", res[1], res[0]);
    } else {
        unsigned char i;
        uint32_t conf[NUM_OTP_CONF];

        for (i = 0; i < NUM_OTP_CONF; ++i) {
            rc = otp_read_config(otp->ahb, i, &conf[i]);
            if (rc)
                goto done;
        }

        logi("OTP configuration:\n");
        for (i = 0; i < NUM_OTP_CONF; ++i)
            logi("%02u: %08x\n", i, conf[i]);
    }

done:
    otp_writel(otp->ahb, OTP_PROTECT_KEY, 0);
    return rc;
}

int otp_write_conf(struct otp *otp, unsigned int word, unsigned int bit)
{
    int rc;
    uint32_t conf;
    uint32_t address;
    uint32_t bitmask;

    if (word >= NUM_OTP_CONF || bit >= 32)
        return -EINVAL;

    bitmask = 1 << bit;

    rc = otp_writel(otp->ahb, OTP_PROTECT_KEY, OTP_PASSWD);
    if (rc)
        return rc;

    rc = otp_read_config(otp->ahb, word, &conf);
    if (rc)
        goto done;

    if (conf & bitmask) {
        loge("Configuration bit already set\n");
        rc = -EALREADY;
        goto done;
    }

    address = 0x800;
    address |= (word / 8) * 0x200;
    address |= (word % 8) * 2;

    logi("Writing configuration at OTP %04x with %08x\n", address, bitmask);
    rc = otp_confirm();
    if (rc)
        goto done;

    rc = otp_write(otp, address, bitmask);

done:
    otp_writel(otp->ahb, OTP_PROTECT_KEY, 0);
    return rc;
}

int otp_write_strap(struct otp *otp, unsigned int bit, unsigned int val)
{
    int i;
    int rc;
    int f = -1;
    uint32_t address;
    uint32_t bitmask;
    uint32_t protect;
    uint32_t res = 0;
    uint32_t word = 0;
    uint32_t strap[6];

    if (bit >= 64 || val > 1)
        return -EINVAL;

    rc = otp_writel(otp->ahb, OTP_PROTECT_KEY, OTP_PASSWD);
    if (rc)
        return rc;

    if (bit > 31) {
        word = 1;
        bit -= 32;
    }

    bitmask = 1 << bit;

    rc = otp_read_config(otp->ahb, 30 + word, &protect);
    if (rc)
        goto done;

    if (protect & bitmask) {
        loge("Cannot write strap; bit is protected\n");
        rc = -EACCES;
        goto done;
    }

    for (i = 0; i < 6; ++i) {
        uint32_t o = 16 + (i * 2);

        rc = otp_read_config(otp->ahb, o + word, &strap[i]);
        if (rc)
            goto done;

        res ^= strap[i];
        if (f < 0 && !(strap[i] & bitmask))
            f = i;
    }

    if (f < 0) {
        loge("Strap cannot be configured further\n");
        rc = -EPERM;
        goto done;
    }

    if (((res & bitmask) && val) || (!val && !(res & bitmask))) {
        loge("Strap already in desired configuration\n");
        rc = -EALREADY;
        goto done;
    }

    i = (16 + (f * 2)) + word;
    address = 0x800;
    address |= (i / 8) * 0x200;
    address |= (i % 8) * 2;

    logi("Writing strap at OTP %04x with %08x\n", address, bitmask);
    rc = otp_confirm();
    if (rc)
        goto done;

    rc = otp_write(otp, address, bitmask);

done:
    otp_writel(otp->ahb, OTP_PROTECT_KEY, 0);
    return rc;
}
