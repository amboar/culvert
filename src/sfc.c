// SPDX-License-Identifier: Apache-2.0
/* Copyright 2013-2014 IBM Corp. */
// Copyright (C) 2021, Oracle and/or its affiliates.

/* Code shamelessly stolen from skiboot and then hacked to death */

#define _GNU_SOURCE
#include "ast.h"
#include "bits.h"
#include "clk.h"
#include "log.h"
#include "sfc.h"

#include "ccan/container_of/container_of.h"

#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef __unused
#define __unused __attribute__((unused))
#endif

#define CALIBRATE_BUF_SIZE	16384

#define SFC_ERR(fmt, ...) loge(fmt, ##__VA_ARGS__)
#define SFC_INF(fmt, ...) logi(fmt, ##__VA_ARGS__)
#define SFC_DBG(fmt, ...) logd(fmt, ##__VA_ARGS__)

/* Firmware Flash Memory Controller */
#define   FMC_CE_TYPE			0x00
#define	     FMC_CE_TYPE_CE2_WP		BIT(18)
#define	     FMC_CE_TYPE_CE1_WP		BIT(17)
#define	     FMC_CE_TYPE_CE0_WP		BIT(16)
#define   FMC_CE_CTRL			0x04
#define   FMC_CE0_CTRL			0x10
#define   FMC_TIMING			0x94

/* SPI (Host) Flash Memory Controller */
#define   SMC_CONF			0x00
#define   SMC_CE0_CTRL			0x10
#define   SMC_TIMING			0x94

struct sfc_data {
    struct soc *soc;
    struct soc_region iomem;
    struct soc_region flash;
    struct clk clk;

    /* We have 2 controllers, one for the BMC flash, one for the PNOR */
    uint8_t    type;

    uint32_t type_reg;
    uint32_t type_wp_mask;

    /* Address and previous value of the ctrl register */
    uint32_t ctl_reg;

    /* Control register value for normal commands */
    uint32_t ctl_val;

    /* Control register value for (fast) reads */
    uint32_t ctl_read_val;

    /* Flash read timing register  */
    uint32_t fread_timing_reg;
    uint32_t fread_timing_val;

    /* Current 4b mode */
    bool mode_4b;

    /* Callbacks */
    struct sfc ops;
};

static inline int
sfc_readl(struct sfc_data *ctx, uint32_t offset, uint32_t *val)
{
    return soc_readl(ctx->soc, ctx->iomem.start + offset, val);
}

static inline int
sfc_writel(struct sfc_data *ctx, uint32_t offset, uint32_t val)
{
    return soc_writel(ctx->soc, ctx->iomem.start + offset, val);
}

static inline ssize_t
flash_read(struct sfc_data *ctx, uint32_t offset, void *buf, size_t len)
{
    return soc_read(ctx->soc, ctx->flash.start + offset, buf, len);
}

static inline ssize_t
flash_write(struct sfc_data *ctx, uint32_t offset, const void *buf, size_t len)
{
    return soc_write(ctx->soc, ctx->flash.start + offset, buf, len);
}

static uint32_t ast_ahb_freq;

static const uint32_t ast_ct_hclk_divs[] = {
    0xf, /* HCLK */
    0x7, /* HCLK/2 */
    0xe, /* HCLK/3 */
    0x6, /* HCLK/4 */
    0xd, /* HCLK/5 */
};

static void fl_micron_status(struct sfc *ct)
{
    uint8_t flst;

    /*
     * After a success status on a write or erase, we
     * need to do that command or some chip variants will
     * lock
     */
    ct->cmd_rd(ct, CMD_MIC_RDFLST, false, 0, &flst, 1);
}

static int fl_read_stat(struct sfc *ct, uint8_t *stat)
{
    return ct->cmd_rd(ct, CMD_RDSR, false, 0, stat, 1);
}

/* Synchronous write completion, probably need a yield hook */
static int fl_sync_wait_idle(struct sfc *ct)
{
    uint8_t stat;
    int rc;

    /* XXX Add timeout */
    for (;;) {
	rc = fl_read_stat(ct, &stat);
	if (rc) return rc;
	if (!(stat & STAT_WIP)) {
	    if (ct->finfo->flags & FL_MICRON_BUGS)
		fl_micron_status(ct);
	    return 0;
	}
	usleep(100);
    }
    /* return FLASH_ERR_WIP_TIMEOUT; */
}

/* Exported for internal use */
static int fl_wren(struct sfc *ct)
{
    int i, rc;
    uint8_t stat;

    /* Some flashes need it to be hammered */
    for (i = 0; i < 1000; i++) {
	rc = ct->cmd_wr(ct, CMD_WREN, false, 0, NULL, 0);
	if (rc) return rc;
	rc = fl_read_stat(ct, &stat);
	if (rc) return rc;
	if (stat & STAT_WIP) {
	    SFC_ERR("LIBFLASH: WREN has WIP status set !\n");
	    rc = fl_sync_wait_idle(ct);
	    if (rc)
		return rc;
	    continue;
	}
	if (stat & STAT_WEN)
	    return 0;
    }
    return -ETIMEDOUT;
}

static int sfc_start_cmd(struct sfc_data *ct, uint8_t cmd)
{
    int rc;

    /* Switch to user mode, CE# dropped */
    rc = sfc_writel(ct, ct->ctl_reg, ct->ctl_val | 7);
    if (rc < 0)
	return rc;

    /* user mode, CE# active */
    rc = sfc_writel(ct, ct->ctl_reg, ct->ctl_val | 3);
    if (rc < 0)
	return rc;

    /* write cmd */
    rc = flash_write(ct, 0, &cmd, 1);

    return rc == 1 ? 0 : rc;
}

static void sfc_end_cmd(struct sfc_data *ct)
{
    int rc;

    /* clear CE# */
    rc = sfc_writel(ct, ct->ctl_reg, ct->ctl_val | 7);
    if (rc < 0) { errno = -rc; perror("sfc_writel"); return; }

    /* Switch back to read mode */
    rc = sfc_writel(ct, ct->ctl_reg, ct->ctl_read_val);
    if (rc < 0) { errno = -rc; perror("sfc_writel"); return; }
}

static int sfc_send_addr(struct sfc_data *ct, uint32_t addr)
{
    const void *ap;
    int rc, len;

    /* Layout address MSB first in memory */
    addr = htobe32(addr);

    /* Send the right amount of bytes */
    ap = (char *)&addr;

    if (ct->mode_4b) {
	len = 4;
	rc = flash_write(ct, 0, ap, len);
    } else {
	len = 3;
	rc = flash_write(ct, 0, ap + 1, len);
    }

    return rc == len ? 0 : rc;
}

static int sfc_cmd_rd(struct sfc *ctrl, uint8_t cmd,
			 bool has_addr, uint32_t addr, void *buffer,
			 uint32_t size)
{
    struct sfc_data *ct = container_of(ctrl, struct sfc_data, ops);
    int rc;

    rc = sfc_start_cmd(ct, cmd);
    if (rc)
	goto bail;
    if (has_addr) {
	rc = sfc_send_addr(ct, addr);
	if (rc)
	    goto bail;
    }
    if (buffer && size) {
        uint32_t i = 0;

        /*
         * Some bridges (P2A and debug UART, probably others too) have a quirk
         * where they'll generate 4 byte reads even when a 1/2 byte read is
         * requested. When the SFC is in user mode it'll clock out one byte for each
         * byte of the MMIO read/write size as a result if we use anything smaller than
         * a 4 byte read we'll lose data. The easiest solution is to just use 4 byte reads
         * for everything and extract the bytes manually when needed.
         *
         * Writes don't have this problem, thankfully.
         */
        while (size) {
            uint8_t *buf = buffer;
            uint32_t val = 0;

            rc = soc_readl(ct->soc, ct->flash.start, &val);
            if (rc)
                goto bail;

            if (size) { buf[i++] = (val >>  0) & 0xff; size--; }
            if (size) { buf[i++] = (val >>  8) & 0xff; size--; }
            if (size) { buf[i++] = (val >> 16) & 0xff; size--; }
            if (size) { buf[i++] = (val >> 24) & 0xff; size--; }
        }
        rc = 0;
    }

bail:
    sfc_end_cmd(ct);

    return rc;
}

static int sfc_cmd_wr(struct sfc *ctrl, uint8_t cmd,
			 bool has_addr, uint32_t addr, const void *buffer,
			 uint32_t size)
{
    struct sfc_data *ct = container_of(ctrl, struct sfc_data, ops);
    ssize_t rc;

    rc = sfc_start_cmd(ct, cmd);
    if (rc)
	goto bail;

    if (has_addr) {
	rc = sfc_send_addr(ct, addr);
	if (rc)
	    goto bail;
    }
    if (buffer && size)
	rc = flash_write(ct, 0, buffer, size);
bail:
    sfc_end_cmd(ct);

    return rc == (ssize_t)size ? 0 : rc;
}

static int sfc_set_4b(struct sfc *ctrl, bool enable)
{
    struct sfc_data *ct = container_of(ctrl, struct sfc_data, ops);
    uint32_t ce_ctrl = 0;
    int rc;

    if (ct->type == SFC_TYPE_FMC && ct->ops.finfo->size > 0x1000000) {
	rc = sfc_readl(ct, FMC_CE_CTRL, &ce_ctrl);
	if (rc < 0)
	    return rc;
    } else if (ct->type != SFC_TYPE_SMC)
	return enable ? -EIO : 0;

    /*
     * We update the "old" value as well since when quitting
     * we don't restore the mode of the flash itself so we need
     * to leave the controller in a compatible setup
     */
    if (enable) {
	ct->ctl_val |= 0x2000;
	ct->ctl_read_val |= 0x2000;
	ce_ctrl |= 0x1;
    } else {
	ct->ctl_val &= ~0x2000;
	ct->ctl_read_val &= ~0x2000;
	ce_ctrl &= ~0x1;
    }
    ct->mode_4b = enable;

    /* Update read mode */
    rc = sfc_writel(ct, ct->ctl_reg, ct->ctl_read_val);
    if (rc < 0)
	return rc;

    if (ce_ctrl && ct->type == SFC_TYPE_FMC)
	return sfc_writel(ct, FMC_CE_CTRL, ce_ctrl);

    return 0;
}

static int sfc_direct_read(struct sfc *ctrl, uint32_t pos, void *buf, uint32_t len)
{
    ssize_t rc;

    struct sfc_data *ct = container_of(ctrl, struct sfc_data, ops);

    /*
     * We are in read mode by default. We don't yet support fancy
     * things like fast read or X2 mode
     */
    if ((rc = flash_read(ct, pos, buf, len)) < 0)
	return rc;

    return (uint32_t)rc == len ? 0 : rc;
}

static void ast2500_get_ahb_freq(struct sfc_data *ct)
{
    if (ast_ahb_freq)
	return;

    ast_ahb_freq = clk_get_rate(&ct->clk, clk_ahb);
}

static int sfc_check_reads(struct sfc_data *ct,
			      const uint8_t *golden_buf, uint8_t *test_buf)
{
    int i, rc;

    for (i = 0; i < 10; i++) {
	rc = flash_read(ct, 0, test_buf, CALIBRATE_BUF_SIZE);
	if (rc)
	    return rc;
	if (memcmp(test_buf, golden_buf, CALIBRATE_BUF_SIZE) != 0)
	    return -EREMOTEIO;
    }
    return 0;
}

static int sfc_calibrate_reads(struct sfc_data *ct, uint32_t hdiv,
				  const uint8_t *golden_buf, uint8_t *test_buf)
{
    int i, rc;
    int good_pass = -1, pass_count = 0;
    uint32_t shift = (hdiv - 1) << 2;
    uint32_t mask = ~(0xfu << shift);

#define FREAD_TPASS(i)	(((i) / 2) | (((i) & 1) ? 0 : 8))

    /* Try HCLK delay 0..5, each one with/without delay and look for a
     * good pair.
     */
    for (i = 0; i < 12; i++) {
	bool pass;

	ct->fread_timing_val &= mask;
	ct->fread_timing_val |= FREAD_TPASS(i) << shift;
	rc = sfc_writel(ct, ct->fread_timing_reg, ct->fread_timing_val);
	if (rc < 0)
	    return rc;

	rc = sfc_check_reads(ct, golden_buf, test_buf);
	if (rc && rc != -EREMOTEIO)
	    return rc;
	pass = (rc == 0);
	SFC_DBG("  * [%08x] %d HCLK delay, %dns DI delay : %s\n",
		ct->fread_timing_val, i/2, (i & 1) ? 0 : 4, pass ? "PASS" : "FAIL");
	if (pass) {
	    pass_count++;
	    if (pass_count == 3) {
		good_pass = i - 1;
		break;
	    }
	} else
	    pass_count = 0;
    }

    /* No good setting for this frequency */
    if (good_pass < 0)
	return -EREMOTEIO;

    /* We have at least one pass of margin, let's use first pass */
    ct->fread_timing_val &= mask;
    ct->fread_timing_val |= FREAD_TPASS(good_pass) << shift;
    rc = sfc_writel(ct, ct->fread_timing_reg, ct->fread_timing_val);
    if (rc < 0)
	return rc;

    SFC_DBG("AST:  * -> good is pass %d [0x%08x]\n",
	    good_pass, ct->fread_timing_val);
    return 0;
}

static bool ast_calib_data_usable(const uint8_t *test_buf, uint32_t size)
{
    const uint32_t *tb32 = (const uint32_t *)test_buf;
    uint32_t i, cnt = 0;

    /* We check if we have enough words that are neither all 0
     * nor all 1's so the calibration can be considered valid.
     *
     * I use an arbitrary threshold for now of 64
     */
    size >>= 2;
    for (i = 0; i < size; i++) {
	if (tb32[i] != 0 && tb32[i] != 0xffffffff)
	    cnt++;
    }
    return cnt >= 64;
}

static int sfc_optimize_reads(struct sfc_data *ct,
				 struct flash_info *info __unused,
				 uint32_t max_freq)
{
    uint8_t *golden_buf, *test_buf;
    int i, rc, best_div = -1;
    uint32_t save_read_val = ct->ctl_read_val;

    test_buf = malloc(CALIBRATE_BUF_SIZE * 2);
    golden_buf = test_buf + CALIBRATE_BUF_SIZE;

    /* We start with the dumbest setting and read some data */
    ct->ctl_read_val = (ct->ctl_read_val & 0x2000) |
	(0x00 << 28) | /* Single bit */
	(0x00 << 24) | /* CE# max */
	(0x03 << 16) | /* use normal reads */
	(0x00 <<  8) | /* HCLK/16 */
	(0x00 <<  6) | /* no dummy cycle */
	(0x00);        /* normal read */
    sfc_writel(ct, ct->ctl_reg, ct->ctl_read_val);

    rc = flash_read(ct, 0, golden_buf, CALIBRATE_BUF_SIZE);
    if (rc) {
	free(test_buf);
	return rc;
    }

    /* Establish our read mode with freq field set to 0 */
    ct->ctl_read_val = save_read_val & 0xfffff0ff;

    /* Check if calibration data is suitable */
    if (!ast_calib_data_usable(golden_buf, CALIBRATE_BUF_SIZE)) {
	SFC_DBG("AST: Calibration area too uniform, "
		"using low speed\n");
	rc = sfc_writel(ct, ct->ctl_reg, ct->ctl_read_val);
	free(test_buf);
	return rc;
    }

    /* Now we iterate the HCLK dividers until we find our breaking point */
    for (i = 5; i > 0; i--) {
	uint32_t tv, freq;

	/* Compare timing to max */
	freq = ast_ahb_freq / i;
	if (freq >= max_freq)
	    continue;

	/* Set the timing */
	tv = ct->ctl_read_val | (ast_ct_hclk_divs[i - 1] << 8);
	rc = sfc_writel(ct, ct->ctl_reg, tv);
	if (rc < 0) {
	    free(test_buf);
	    return rc;
	}
	SFC_DBG("AST: Trying HCLK/%d...\n", i);
	rc = sfc_calibrate_reads(ct, i, golden_buf, test_buf);

	/* Some other error occurred, bail out */
	if (rc && rc != -EREMOTEIO) {
	    free(test_buf);
	    return rc;
	}
	if (rc == 0)
	    best_div = i;
    }
    free(test_buf);

    /* Nothing found ? */
    if (best_div < 0)
	SFC_INF("AST: No good frequency, using dumb slow\n");
    else {
	SFC_INF("AST: Found good read timings at HCLK/%d\n", best_div);
	ct->ctl_read_val |= (ast_ct_hclk_divs[best_div - 1] << 8);
    }
    return sfc_writel(ct, ct->ctl_reg, ct->ctl_read_val);
}

static int sfc_get_hclk(uint32_t *ctl_val, uint32_t max_freq)
{
    int i;

    /* It appears that running commands at HCLK/2 on some micron
     * chips results in occasionally reads of bogus status (that
     * or unrelated chip hangs).
     *
     * Since we cannot calibrate properly the reads for commands,
     * instead, let's limit our SPI frequency to HCLK/4 to stay
     * on the safe side of things
     */
#define MIN_CMD_FREQ	4
    for (i = MIN_CMD_FREQ; i <= 5; i++) {
	uint32_t freq = ast_ahb_freq / i;
	if (freq >= max_freq)
	    continue;
	*ctl_val |= (ast_ct_hclk_divs[i - 1] << 8);
	return i;
    }
    return 0;
}

static int sfc_setup_macronix(struct sfc_data *ct, struct flash_info *info)
{
    int rc, div __unused;
    uint8_t srcr[2];

    /*
     * Those Macronix chips support dual reads at 104Mhz
     * and dual IO at 84Mhz with 4 dummies.
     *
     * Our calibration algo should give us something along
     * the lines of HCLK/3 (HCLK/2 seems to work sometimes
     * but appears to be fairly unreliable) which is 64Mhz
     *
     * So we chose dual IO mode.
     *
     * The CE# inactive width for reads must be 7ns, we set it
     * to 3T which is about 15ns at the fastest speed we support
     * HCLK/2) as I've had issue with smaller values.
     *
     * For write and program it's 30ns so let's set the value
     * for normal ops to 6T.
     *
     * Preserve the current 4b mode.
     */
    SFC_DBG("AST: Setting up Macronix...\n");

    /*
     * Read the status and config registers
     */
    rc = sfc_cmd_rd(&ct->ops, CMD_RDSR, false, 0, &srcr[0], 1);
    if (rc != 0) {
	SFC_ERR("AST: Failed to read status\n");
	return rc;
    }
    rc = sfc_cmd_rd(&ct->ops, CMD_RDCR, false, 0, &srcr[1], 1);
    if (rc != 0) {
	SFC_ERR("AST: Failed to read configuration\n");
	return rc;
    }

    SFC_DBG("AST: Macronix SR:CR: 0x%02x:%02x\n", srcr[0], srcr[1]);

    /* Switch to 8 dummy cycles to enable 104Mhz operations */
    srcr[1] = (srcr[1] & 0x3f) | 0x80;

    rc = fl_wren(&ct->ops);
    if (rc) {
	SFC_ERR("AST: Failed to WREN for Macronix config\n");
	return rc;
    }

    rc = sfc_cmd_wr(&ct->ops, CMD_WRSR, false, 0, srcr, 2);
    if (rc != 0) {
	SFC_ERR("AST: Failed to write Macronix config\n");
	return rc;
    }
    rc = fl_sync_wait_idle(&ct->ops);;
    if (rc != 0) {
	SFC_ERR("AST: Failed waiting for config write\n");
	return rc;
    }

    SFC_DBG("AST: Macronix SR:CR: 0x%02x:%02x\n", srcr[0], srcr[1]);

    /* Use 2READ */
    ct->ctl_read_val = (ct->ctl_read_val & 0x2000) |
	(0x03 << 28) | /* Dual IO */
	(0x0d << 24) | /* CE# width 3T */
	(0xbb << 16) | /* 2READ command */
	(0x00 <<  8) | /* HCLK/16 (optimize later) */
	(0x02 <<  6) | /* 2 bytes dummy cycle (8 clocks) */
	(0x01);	       /* fast read */

    /* Configure SPI flash read timing */
    rc = sfc_optimize_reads(ct, info, 104000000);
    if (rc) {
	SFC_ERR("AST: Failed to setup proper read timings, rc=%d\n", rc);
	return rc;
    }

    /*
     * For other commands and writes also increase the SPI clock
     * to HCLK/2 since the chip supports up to 133Mhz and set
     * CE# inactive to 6T. We request a timing that is 20% below
     * the limit of the chip, so about 106Mhz which should fit.
     */
    ct->ctl_val = (ct->ctl_val & 0x2000) |
	(0x00 << 28) | /* Single bit */
	(0x0a << 24) | /* CE# width 6T (b1010) */
	(0x00 << 16) | /* no command */
	(0x00 <<  8) | /* HCLK/16 (done later) */
	(0x00 <<  6) | /* no dummy cycle */
	(0x00);	       /* normal read */

    div = sfc_get_hclk(&ct->ctl_val, 106000000);
    SFC_INF("AST: Command timing set to HCLK/%d\n", div);

    /* Update chip with current read config */
    return sfc_writel(ct, ct->ctl_reg, ct->ctl_read_val);
}

static int sfc_setup_winbond(struct sfc_data *ct, struct flash_info *info)
{
    int rc, div __unused;

    SFC_DBG("AST: Setting up Windbond...\n");

    /*
     * This Windbond chip support dual reads at 104Mhz
     * with 8 dummy cycles.
     *
     * The CE# inactive width for reads must be 10ns, we set it
     * to 3T which is about 15.6ns.
     */
    ct->ctl_read_val = (ct->ctl_read_val & 0x2000) |
	(0x02 << 28) | /* Dual bit data only */
	(0x0e << 24) | /* CE# width 2T (b1110) */
	(0x3b << 16) | /* DREAD command */
	(0x00 <<  8) | /* HCLK/16 */
	(0x01 <<  6) | /* 1-byte dummy cycle */
	(0x01);	       /* fast read */

    /* Configure SPI flash read timing */
    rc = sfc_optimize_reads(ct, info, 104000000);
    if (rc) {
	SFC_ERR("AST: Failed to setup proper read timings, rc=%d\n", rc);
	return rc;
    }

    /*
     * For other commands and writes also increase the SPI clock
     * to HCLK/2 since the chip supports up to 133Mhz. CE# inactive
     * for write and erase is 50ns so let's set it to 10T.
     */
    ct->ctl_val = (ct->ctl_read_val & 0x2000) |
	(0x00 << 28) | /* Single bit */
	(0x06 << 24) | /* CE# width 10T (b0110) */
	(0x00 << 16) | /* no command */
	(0x00 <<  8) | /* HCLK/16 */
	(0x00 <<  6) | /* no dummy cycle */
	(0x01);	       /* fast read */

    div = sfc_get_hclk(&ct->ctl_val, 106000000);
    SFC_ERR("AST: Command timing set to HCLK/%d\n", div);

    /* Update chip with current read config */
    return sfc_writel(ct, ct->ctl_reg, ct->ctl_read_val);
}

static int sfc_setup_micron(struct sfc_data *ct, struct flash_info *info)
{
    uint8_t	vconf, ext_id[6];
    int rc, div __unused;

    SFC_DBG("AST: Setting up Micron...\n");

    /*
     * Read the extended chip ID to try to detect old vs. new
     * flashes since old Micron flashes have a lot of issues
     */
    rc = sfc_cmd_rd(&ct->ops, CMD_RDID, false, 0, ext_id, 6);
    if (rc != 0) {
	SFC_ERR("AST: Failed to read Micron ext ID, sticking to dumb speed\n");
	return 0;
    }
    /* Check ID matches expectations */
    if (ext_id[0] != ((info->id >> 16) & 0xff) ||
	    ext_id[1] != ((info->id >>	8) & 0xff) ||
	    ext_id[2] != ((info->id	 ) & 0xff)) {
	SFC_ERR("AST: Micron ext ID mismatch, sticking to dumb speed\n");
	return 0;
    }
    SFC_DBG("AST: Micron ext ID byte: 0x%02x\n", ext_id[4]);

    /* Check for old (<45nm) chips, don't try to be fancy on those */
    if (!(ext_id[4] & 0x40)) {
	SFC_DBG("AST: Old chip, using dumb timings\n");
	goto dumb;
    }

    /*
     * Read the micron specific volatile configuration reg
     */
    rc = sfc_cmd_rd(&ct->ops, CMD_MIC_RDVCONF, false, 0, &vconf, 1);
    if (rc != 0) {
	SFC_ERR("AST: Failed to read Micron vconf, sticking to dumb speed\n");
	goto dumb;
    }
    SFC_DBG("AST: Micron VCONF: 0x%02x\n", vconf);

    /* Switch to 8 dummy cycles (we might be able to operate with 4
     * but let's keep some margin
     */
    vconf = (vconf & 0x0f) | 0x80;

    rc = sfc_cmd_wr(&ct->ops, CMD_MIC_WRVCONF, false, 0, &vconf, 1);
    if (rc != 0) {
	SFC_ERR("AST: Failed to write Micron vconf, "
		" sticking to dumb speed\n");
	goto dumb;
    }
    rc = fl_sync_wait_idle(&ct->ops);;
    if (rc != 0) {
	SFC_ERR("AST: Failed waiting for config write\n");
	return rc;
    }
    SFC_DBG("AST: Updated to  : 0x%02x\n", vconf);

    /*
     * Try to do full dual IO, with 8 dummy cycles it supports 133Mhz
     *
     * The CE# inactive width for reads must be 20ns, we set it
     * to 4T which is about 20.8ns.
     */
    ct->ctl_read_val = (ct->ctl_read_val & 0x2000) |
	(0x03 << 28) | /* Single bit */
	(0x0c << 24) | /* CE# 4T */
	(0xbb << 16) | /* 2READ command */
	(0x00 <<  8) | /* HCLK/16 (optimize later) */
	(0x02 <<  6) | /* 8 dummy cycles (2 bytes) */
	(0x01);	       /* fast read */

    /* Configure SPI flash read timing */
    rc = sfc_optimize_reads(ct, info, 133000000);
    if (rc) {
	SFC_ERR("AST: Failed to setup proper read timings, rc=%d\n", rc);
	return rc;
    }

    /*
     * For other commands and writes also increase the SPI clock
     * to HCLK/2 since the chip supports up to 133Mhz. CE# inactive
     * for write and erase is 50ns so let's set it to 10T.
     */
    ct->ctl_val = (ct->ctl_read_val & 0x2000) |
	(0x00 << 28) | /* Single bit */
	(0x06 << 24) | /* CE# width 10T (b0110) */
	(0x00 << 16) | /* no command */
	(0x00 <<  8) | /* HCLK/16 */
	(0x00 <<  6) | /* no dummy cycle */
	(0x00);	       /* norm read */

    div = sfc_get_hclk(&ct->ctl_val, 133000000);
    SFC_INF("AST: Command timing set to HCLK/%d\n", div);

    /* Update chip with current read config */
    return sfc_writel(ct, ct->ctl_reg, ct->ctl_read_val);

dumb:
    ct->ctl_val = ct->ctl_read_val = (ct->ctl_read_val & 0x2000) |
	(0x00 << 28) | /* Single bit */
	(0x00 << 24) | /* CE# max */
	(0x03 << 16) | /* use normal reads */
	(0x06 <<  8) | /* HCLK/4 */
	(0x00 <<  6) | /* no dummy cycle */
	(0x00);	       /* normal read */

    /* Update chip with current read config */
    return sfc_writel(ct, ct->ctl_reg, ct->ctl_read_val);
}

static int sfc_setup(struct sfc *ctrl, uint32_t *tsize)
{
    struct sfc_data *ct = container_of(ctrl, struct sfc_data, ops);
    struct flash_info *info = ctrl->finfo;

    (void)tsize;

    return 0;

    /*
     * Configure better timings and read mode for known
     * flash chips
     */
    switch(info->id) {
    case 0xc22018: /* MX25L12835F */
    case 0xc22019: /* MX25L25635F */
    case 0xc2201a: /* MX66L51235F */
    case 0xc2201b: /* MX66L1G45G */
	return sfc_setup_macronix(ct, info);
    case 0xef4018: /* W25Q128BV */
    case 0xef4019: /* W25Q256BV */
	return sfc_setup_winbond(ct, info);
    case 0x20ba20: /* MT25Qx512xx */
	return sfc_setup_micron(ct, info);
    }
    /* No special tuning */
    return 0;
}

static bool sfc_init_device(struct sfc_data *ct)
{
    uint32_t ce_type;
    int rc;

    /*
     * Snapshot control reg and sanitize it for our
     * use, switching to 1-bit mode, clearing user
     * mode if set, etc...
     *
     * Also configure SPI clock to something safe
     * like HCLK/8 (24Mhz)
     */
    rc = sfc_readl(ct, ct->ctl_reg, &ct->ctl_val);
    if (rc < 0 || ct->ctl_val == 0xffffffff) {
	SFC_ERR("AST_SF: Failed read from controller control\n");
	return false;
    }

    /* Enable writes for user mode */
    rc = sfc_readl(ct, ct->type_reg, &ce_type);
    if (rc < 0) { errno = -rc; perror("sfc_readl"); return false; }

    rc = sfc_writel(ct, ct->type_reg, ce_type | (7 << 16));
    if (rc < 0) { errno = -rc; perror("sfc_writel"); return false; }

    ct->ctl_val =
	(0x00 << 28) | /* Single bit */
	(0x00 << 24) | /* CE# width 16T */
	(0x00 << 16) | /* no command */
	(0x04 <<  8) | /* HCLK/8 */
	(0x00 <<  6) | /* no dummy cycle */
	(0x00);	       /* normal read */

    /* Initial read mode is default */
    ct->ctl_read_val = ct->ctl_val;

    /* Initial read timings all 0 */
    ct->fread_timing_val = 0;

    /* Configure for read */
    rc = sfc_writel(ct, ct->ctl_reg, ct->ctl_read_val);
    if (rc < 0) { errno = -rc; perror("sfc_writel"); return false; }
    rc = sfc_writel(ct, ct->fread_timing_reg, ct->fread_timing_val);
    if (rc < 0) { errno = -rc; perror("sfc_writel"); return false; }

    ct->mode_4b = false;

    return true;
}

int sfc_write_protect_save(struct sfc *ctrl, bool enable, uint32_t *save)
{
    struct sfc_data *ct = container_of(ctrl, struct sfc_data, ops);
    uint32_t old_tsr, new_tsr;
    int rc;

    if ((rc = sfc_readl(ct, ct->type_reg, &old_tsr)) < 0)
	return rc;

    /* TODO: Specify which CE? */

    if (enable) {
	new_tsr = old_tsr | ct->type_wp_mask;
    } else {
	new_tsr = old_tsr & ~ct->type_wp_mask;
    }

    if ((rc = sfc_writel(ct, ct->type_reg, new_tsr)) < 0)
	return rc;

    *save = old_tsr & ct->type_wp_mask;

    return 0;
}

int sfc_write_protect_restore(struct sfc *ctrl, uint32_t save)
{
    struct sfc_data *ct = container_of(ctrl, struct sfc_data, ops);
    uint32_t tsr;
    int rc;

    if ((rc = sfc_readl(ct, ct->type_reg, &tsr)) < 0)
	return rc;

    /* TODO: Specify which CE? */
    tsr &= ~ct->type_wp_mask;
    tsr |= (save & ct->type_wp_mask);

    return sfc_writel(ct, ct->type_reg, tsr);
}

int sfc_get_flash(struct sfc *ctrl, struct soc_region *flash)
{
    struct sfc_data *ct = container_of(ctrl, struct sfc_data, ops);

    *flash = ct->flash;

    return 0;
}

static const struct soc_device_id sfc_match[] = {
    { .compatible = "aspeed,ast2500-fmc", .data = (void *)SFC_TYPE_FMC },
    { .compatible = "aspeed,ast2500-spi", .data = (void *)SFC_TYPE_SMC },
    { },
};

int sfc_init(struct sfc **ctrl, struct soc *soc, const char *name)
{
    struct soc_device_node dn;
    struct sfc_data *ct;
    int rc;

    *ctrl = NULL;
    ct = malloc(sizeof(*ct));
    if (!ct) {
	SFC_ERR("AST_SF: Failed to allocate\n");
	return -ENOMEM;
    }
    memset(ct, 0, sizeof(*ct));

    rc = soc_device_from_name(soc, name, &dn);
    if (rc < 0) {
        loge("sfc: Failed to find device by name '%s': %d\n", name, rc);
	goto fail;
    }

    rc = soc_device_is_compatible(soc, sfc_match, &dn);
    if (rc < 0) {
        loge("sfc: Failed verify device compatibility: %d\n", rc);
	goto fail;
    }

    if (!rc) {
        loge("sfc: Incompatible device described by node '%s'\n", name);
        rc = -EINVAL;
	goto fail;
    }

    if ((rc = soc_device_get_memory_index(soc, &dn, 0, &ct->iomem)) < 0)
	goto fail;

    if ((rc = soc_device_get_memory_index(soc, &dn, 1, &ct->flash)) < 0)
	goto fail;

    ct->type = (unsigned long)(soc_device_get_match_data(soc, sfc_match, &dn));
    if (!ct->type) {
	loge("sfc: Failed to acquire match data for '%s'\n", name);
	rc = -EINVAL;
	goto fail;
    }

    if ((rc = clk_init(&ct->clk, soc)) < 0)
	goto fail;

    ct->soc = soc;

    /* XXX: Hack exposing the AHB instance used to the flash layer */
    ct->ops.priv = soc->ahb;

    ct->ops.cmd_wr = sfc_cmd_wr;
    ct->ops.cmd_rd = sfc_cmd_rd;
    ct->ops.set_4b = sfc_set_4b;
    ct->ops.direct_read = sfc_direct_read;
    ct->ops.setup = sfc_setup;

    ast2500_get_ahb_freq(ct);

    /* TODO: Set these in the platform data, add platform data pointer to ct */
    if (ct->type == SFC_TYPE_SMC) {
	ct->type_reg = FMC_CE_TYPE;
	ct->type_wp_mask = (FMC_CE_TYPE_CE0_WP | FMC_CE_TYPE_CE1_WP |
			    FMC_CE_TYPE_CE2_WP);
	ct->ctl_reg = SMC_CE0_CTRL;
	ct->fread_timing_reg = SMC_TIMING;
    } else if (ct->type == SFC_TYPE_FMC) {
	ct->type_reg = FMC_CE_TYPE;
	ct->type_wp_mask = (FMC_CE_TYPE_CE0_WP | FMC_CE_TYPE_CE1_WP);
	ct->ctl_reg = FMC_CE0_CTRL;
	ct->fread_timing_reg = FMC_TIMING;
    } else {
	rc = -EINVAL;
	goto cleanup_clk;
    }

    if (!sfc_init_device(ct)) {
	rc = -EIO;
	goto cleanup_clk;
    }

    *ctrl = &ct->ops;

    return 0;

cleanup_clk:
    clk_destroy(&ct->clk);

fail:
    free(ct);
    return rc;
}

int sfc_destroy(struct sfc *ctrl)
{
	struct sfc_data *ct = container_of(ctrl, struct sfc_data, ops);
	int rc;

	/* Restore control reg to read */
	rc = sfc_writel(ct, ct->ctl_reg, ct->ctl_read_val);
	if (rc < 0)
		return rc;

	/* Additional cleanup */
	if (ct->type == SFC_TYPE_SMC) {
		uint32_t reg;
		rc = sfc_readl(ct, SMC_CONF, &reg);
		if (rc < 0)
			return rc;

		if (reg != 0xffffffff) {
			rc = sfc_writel(ct, SMC_CONF, reg & ~1);
			if (rc < 0)
				return rc;
		}
	}

	/* Free the whole lot */
	free(ct);

	return 0;
}
