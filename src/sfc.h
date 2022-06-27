/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */
// Copyright (C) 2021, Oracle and/or its affiliates.

#ifndef _SFC_H
#define _SFC_H

#include "soc.h"

#include <stdbool.h>

#define SFC_TYPE_FMC 		1U
#define SFC_TYPE_SMC 		2U

/* Flash commands */
#define CMD_BE			0xd8	/* Block (64K) Erase */
#define CMD_BE32K		0x52	/* Block (32K) Erase */
#define CMD_CE			0x60	/* Chip Erase (Macronix/Winbond) */
#define CMD_EN4B		0xb7	/* Enable 4B addresses */
#define CMD_EX4B		0xe9	/* Exit 4B addresses */
#define CMD_MIC_BULK_ERASE	0xc7	/* Micron Bulk Erase */
#define CMD_MIC_RDFLST		0x70	/* Micron Read Flag Status */
#define CMD_MIC_RDVCONF		0x85	/* Micron Read Volatile Config */
#define CMD_MIC_WRVCONF		0x81	/* Micron Write Volatile Config */
#define CMD_PP			0x02	/* Page Program */
#define CMD_RDCR		0x15	/* Read configuration register (Macronix) */
#define CMD_RDID		0x9f	/* Read JEDEC ID */
#define CMD_RDSR		0x05	/* Read Status Register */
#define CMD_READ		0x03	/* READ */
#define CMD_SE			0x20	/* Sector (4K) Erase */
#define CMD_WREN		0x06	/* Write Enable */
#define CMD_WRSR		0x01	/* Write Status Register (also config. on Macronix) */

/* Flash status bits */
#define STAT_WIP	0x01
#define STAT_WEN	0x02

/* This isn't exposed to clients but is to controllers */
struct flash_info {
	uint32_t	id;
	uint32_t	size;
	uint32_t	flags;
#define FL_ERASE_4K	0x00000001	/* Supports 4k erase */
#define FL_ERASE_32K	0x00000002	/* Supports 32k erase */
#define FL_ERASE_64K	0x00000004	/* Supports 64k erase */
#define FL_ERASE_CHIP	0x00000008	/* Supports 0x60 cmd chip erase */
#define FL_ERASE_BULK	0x00000010	/* Supports 0xc7 cmd bulk erase */
#define FL_MICRON_BUGS	0x00000020	/* Various micron bug workarounds */
#define FL_ERASE_ALL	(FL_ERASE_4K | FL_ERASE_32K | FL_ERASE_64K | \
			 FL_ERASE_CHIP)
#define FL_CAN_4B	0x00000010	/* Supports 4b mode */
	const char	*name;
};

/* Flash controller, return negative values for errors */
struct sfc {
	int (*setup)(struct sfc *ctrl, uint32_t *tsize);
	int (*set_4b)(struct sfc *ctrl, bool enable);
	int (*chip_id)(struct sfc *ctrl, uint8_t *id_buf,
		       uint32_t *id_size);
	int (*direct_read)(struct sfc *ctrl, uint32_t addr, void *buf,
		    uint32_t size);
	int (*write)(struct sfc *ctrl, uint32_t addr,
		     const void *buf, uint32_t size);
	int (*erase)(struct sfc *ctrl, uint32_t addr,
		     uint32_t size);
	int (*cmd_rd)(struct sfc *ctrl, uint8_t cmd,
		      bool has_addr, uint32_t addr, void *buffer,
		      uint32_t size);
	int (*cmd_wr)(struct sfc *ctrl, uint8_t cmd,
		      bool has_addr, uint32_t addr, const void *buffer,
		      uint32_t size);
	struct flash_info *finfo;

	void *priv;
};

int sfc_write_protect_save(struct sfc *ctrl, bool enable, uint32_t *save);
int sfc_write_protect_restore(struct sfc *ctrl, uint32_t save);

int sfc_get_flash(struct sfc *ctrl, struct soc_region *flash);

struct sfc *sfc_get_by_name(struct soc *soc, const char *name);

#endif
