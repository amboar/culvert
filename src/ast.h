/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _AST_H
#define _AST_H

#include <stdbool.h>

#include "ahb.h"

enum ast_generation { ast_g4, ast_g5, ast_g6 };

/* AST2500 Memory Space Allocations */
#define AST_G5_SOC_IO			0x1e600000
#define AST_G5_SOC_IO_LEN		0x00200000

#define AST_G5_FMC			0x1e620000
#define   FMC_CE_TYPE			0x00
#define   FMC_CE_CTRL			0x04
#define   FMC_CE0_CTRL			0x10
#define   FMC_TIMING			0x94
#define AST_G5_SMC			0x1e630000
#define   SMC_CONF			0x00
#define   SMC_CE0_CTRL			0x10
#define   SMC_TIMING			0x94
#define AST_G5_SDMC                     0x1e6e0000
#define   SDMC_GMP                      0x08
#define     SDMC_GMP_G5_XDMA            (1 << 17)
#define     SDMC_GMP_G4_XDMA            (1 << 16)
#define AST_G5_SCU			0x1e6e2000
#define   SCU_MISC			0x2c
#define     SCU_MISC_G4_P2A_DRAM_RO	(1 << 25)
#define     SCU_MISC_G4_P2A_SPI_RO	(1 << 24)
#define     SCU_MISC_G4_P2A_SOC_RO	(1 << 23)
#define     SCU_MISC_G4_P2A_FMC_RO	(1 << 22)
#define     SCU_MISC_G5_P2A_DRAM_RO	(1 << 25)
#define     SCU_MISC_G5_P2A_LPCH_RO	(1 << 24)
#define     SCU_MISC_G5_P2A_SOC_RO	(1 << 23)
#define     SCU_MISC_G5_P2A_FLASH_RO	(1 << 22)
#define     SCU_MISC_UART_DBG	        (1 << 10)
#define   SCU_MISC2		        0x4c
#define     SCU_MISC2_UART_DBG_1M	(1 << 30)
#define   SCU_HW_STRAP			0x70
#define     SCU_HW_STRAP_UART_DBG_SEL	(1 << 29)
#define     SCU_HW_STRAP_SIO_DEC	(1 << 20)
#define     SCU_HW_STRAP_ARM_CLK	(1 <<  0)
#define   SCU_SILICON_REVISION		0x7c
#define   SCU_PCIE_CONFIG		0x180
#define     SCU_PCIE_CONFIG_BMC_XDMA    (1 << 14)
#define     SCU_PCIE_CONFIG_BMC_MMIO	(1 << 9)
#define     SCU_PCIE_CONFIG_BMC		(1 << 8)
#define     SCU_PCIE_CONFIG_VGA_XDMA    (1 << 6)
#define     SCU_PCIE_CONFIG_VGA_MMIO	(1 << 1)
#define     SCU_PCIE_CONFIG_VGA		(1 << 0)
#define   SCU_PCIE_MMIO_CONFIG		0x184
#define AST_G5_WDT			0x1e785000
#define   WDT_SIZE			0x20
#define	  WDT_RELOAD			0x04
#define   WDT_RESTART                   0x08
#define   WDT_CTRL			0x0c
#define     WDT_CTRL_BOOT_1             (0 << 7)
#define     WDT_CTRL_BOOT_2             (1 << 7)
#define     WDT_CTRL_RESET_SOC          (0b00 << 5)
#define     WDT_CTRL_RESET_SYS          (0b01 << 5)
#define     WDT_CTRL_RESET_CPU          (0b10 << 5)
#define	    WDT_CTRL_CLK_1MHZ		(1 << 4)
#define     WDT_CTRL_SYS_RESET		(1 << 1)
#define     WDT_CTRL_ENABLE		(1 << 0)
#define   WDT_RESET_MASK		0x1c
#define AST_G5_LPC			0x1e789000
#define   LPC_HICR9			0x98
#define     LPC_HICR9_SEL6IO		(0b1111 << 8)
#define   LPC_HICRA			0x9c
#define     LPC_HICRA_SEL5DW		(0b1111 << 28)
#define     LPC_HICRA_SEL4DW		(0b111 << 25)
#define     LPC_HICRA_SEL3DW		(0b111 << 22)
#define     LPC_HICRA_SEL2DW		(0b111 << 19)
#define     LPC_HICRA_SEL1DW		(0b111 << 16)
#define     LPC_HICRA_SEL5IO		(0b111 << 12)
#define     LPC_HICRA_SEL4IO		(0b111 << 9)
#define     LPC_HICRA_SEL3IO		(0b111 << 6)
#define     LPC_HICRA_SEL2IO		(0b111 << 3)
#define     LPC_HICRA_SEL1IO		(0b111 << 0)
#define   LPC_HICRB			0x100
#define     LPC_HICRB_ILPC_RO		(1 << 6)
#define AST_G5_BMC_FLASH  		0x20000000
#define AST_G5_HOST_FLASH		0x30000000
#define AST_G5_DRAM       		0x80000000

extern const uint32_t bmc_dram_sizes[4];
extern const uint32_t bmc_vram_sizes[4];

enum ast_ip_state {
    ip_state_unknown,
    ip_state_absent,
    ip_state_enabled,
    ip_state_disabled,
};

extern const char *ast_ip_state_desc[4];

struct ast_cap_lpc {
    enum ast_ip_state superio;
    struct ahb_range ilpc;
};

enum ast_p2ab_ranges {
    p2ab_fw,
    p2ab_soc,
    p2ab_fmc,
    p2ab_spi,
    p2ab_rsvd,
    p2ab_lpch,
    p2ab_dram,
    p2ab_ranges_max,
};

struct ast_cap_pci {
    enum ast_ip_state vga;
    enum ast_ip_state vga_mmio;
    enum ast_ip_state vga_xdma;
    enum ast_ip_state bmc;
    enum ast_ip_state bmc_mmio;
    enum ast_ip_state bmc_xdma;
    struct ahb_range ranges[p2ab_ranges_max];
};

enum debug_uart { debug_uart1, debug_uart5 };

struct ast_cap_uart {
    enum ast_ip_state debug;
    enum debug_uart uart;
};

struct ast_cap_kernel {
    bool have_devmem;
};

struct ast_cap_xdma {
    bool unconstrained;
};

struct ast_interfaces {
    struct ast_cap_lpc lpc;
    struct ast_cap_pci pci;
    struct ast_cap_uart uart;
    struct ast_cap_kernel kernel;
    struct ast_cap_xdma xdma;
};

int ast_ahb_bridge_probe(struct ast_interfaces *state);
int ast_ahb_bridge_discover(struct ahb *ahb, struct ast_interfaces *state);
int ast_ahb_init(struct ahb *ahb, bool rw);

int ast_ahb_from_args(struct ahb *ahb, int argc, char *argv[]);
int ast_ahb_access(const char *name, int argc, char *argv[], struct ahb *ahb);

#endif
