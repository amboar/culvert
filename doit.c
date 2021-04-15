// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/types.h>
#include <unistd.h>

#include "ahb.h"
#include "ast.h"
#include "clk.h"
#include "debug.h"
#include "devmem.h"
#include "flash.h"
#include "ilpc.h"
#include "l2a.h"
#include "log.h"
#include "lpc.h"
#include "otp.h"
#include "p2a.h"
#include "mb.h"
#include "priv.h"
#include "prompt.h"
#include "sfc.h"
#include "sio.h"
#include "uart/suart.h"
#include "uart/mux.h"
#include "uart/vuart.h"
#include "wdt.h"

/* Buffer sizes */
#define BMC_FLASH_LEN   (32 << 20)
#define DUMP_RAM_WIN  (8 << 20)
#define SFC_FLASH_WIN (64 << 10)

int cmd_ilpc(const char *name, int argc, char *argv[]);

static void help(const char *name)
{
    printf("%s: " VERSION "\n", name);
    printf("Usage:\n");
    printf("\n");
    printf("%s probe [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s ilpc read ADDRESS\n", name);
    printf("%s ilpc write ADDRESS VALUE\n", name);
    printf("%s p2a vga read ADDRESS\n", name);
    printf("%s p2a vga write ADDRESS VALUE\n", name);
    printf("%s debug read ADDRESS INTERFACE [IP PORT USERNAME PASSWORD]\n", name);
    printf("%s debug write ADDRESS VALUE INTERFACE [IP PORT USERNAME PASSWORD]\n", name);
    printf("%s devmem read ADDRESS\n", name);
    printf("%s devmem write ADDRESS VALUE\n", name);
    printf("%s console HOST_UART BMC_UART BAUD USER PASSWORD\n", name);
    printf("%s read firmware [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s read ram [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s write firmware [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s replace ram MATCH REPLACE\n", name);
    printf("%s reset TYPE [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s sfc fmc read ADDRESS LENGTH [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s sfc fmc erase ADDRESS LENGTH [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s sfc fmc write ADDRESS LENGTH [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s otp read conf [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s otp read strap [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s otp write strap BIT VALUE [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
    printf("%s otp write conf WORD BIT [INTERFACE [IP PORT USERNAME PASSWORD]]\n", name);
}

static int cmd_p2a(const char *name, int argc, char *argv[])
{
    struct p2ab _p2ab, *p2ab = &_p2ab;
    struct ahb _ahb, *ahb = &_ahb;
    int cleanup;
    int rc;

    if (!strcmp("vga", argv[0]))
        rc = p2ab_init(p2ab, AST_PCI_VID, AST_PCI_DID_VGA);
    else if (!strcmp("bmc", argv[0]))
        rc = p2ab_init(p2ab, AST_PCI_VID, AST_PCI_DID_BMC);
    else {
        loge("Unknown PCIe device: %s\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (rc < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(name);
        } else {
            errno = -rc;
            perror("p2ab_init");
        }
        exit(EXIT_FAILURE);
    }

    ahb_use(ahb, ahb_p2ab, p2ab);
    rc = ast_ahb_access(name, argc - 1, argv + 1, ahb);
    if (rc) {
        errno = -rc;
        perror("ast_ahb_access");
        exit(EXIT_FAILURE);
    }

    cleanup = p2ab_destroy(p2ab);
    if (cleanup) {
        errno = -cleanup;
        perror("p2ab_destroy");
        exit(EXIT_FAILURE);
    }

    return 0;
}

static int cmd_debug(const char *name, int argc, char *argv[])
{
    struct debug _debug, *debug = &_debug;
    struct ahb _ahb, *ahb = &_ahb;
    int rc, cleanup;

    /* ./doit debug read 0x1e6e207c digi,portserver-ts-16 <IP> <SERIAL PORT> <USER> <PASSWORD> */
    if (!argc) {
        loge("Not enough arguments for debug command\n");
        help(name);
        exit(EXIT_FAILURE);
    }

    logi("Initialising debug interface\n");
    if (!strcmp("read", argv[0])) {
        if (argc == 3) {
            rc = debug_init(debug, argv[2]);
        } else if (argc == 7) {
            rc = debug_init(debug, argv[2], argv[3], atoi(argv[4]), argv[5],
                            argv[6]);
        } else {
            loge("Incorrect arguments for debug command\n");
            help(name);
            exit(EXIT_FAILURE);
        }
    } else if (!strcmp("write", argv[0])) {
        if (argc == 4) {
            rc = debug_init(debug, argv[3]);
        } else if (argc == 8) {
            rc = debug_init(debug, argv[3], argv[4], atoi(argv[5]), argv[6],
                            argv[7]);
        } else {
            loge("Incorrect arguments for debug command\n");
            help(name);
            exit(EXIT_FAILURE);
        }
    } else {
        loge("Unsupported command: %s\n", argv[0]);
        help(name);
        exit(EXIT_FAILURE);
    }

    if (rc < 0) {
        errno = -rc;
        perror("debug_init");
        exit(EXIT_FAILURE);
    }

    rc = debug_enter(debug);
    if (rc < 0) { errno = -rc; perror("debug_enter"); goto cleanup_debug; }

    ahb_use(ahb, ahb_debug, debug);
    rc = ast_ahb_access(name, argc, argv, ahb);
    if (rc) {
        errno = -rc;
        perror("ast_ahb_access");
        exit(EXIT_FAILURE);
    }

    cleanup = debug_exit(debug);
    if (cleanup < 0) { errno = -cleanup; perror("debug_exit"); }

cleanup_debug:
    logi("Destroying debug interface\n");
    cleanup = debug_destroy(debug);
    if (cleanup < 0) { errno = -cleanup; perror("debug_destroy"); }

    return rc;
}

static int cmd_devmem(const char *name, int argc, char *argv[])
{
    struct devmem _devmem, *devmem = &_devmem;
    struct ahb _ahb, *ahb = &_ahb;
    int cleanup;
    int rc;

    rc = devmem_init(devmem);
    if (rc < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(name);
        } else {
            errno = -rc;
            perror("devmem_init");
        }
        exit(EXIT_FAILURE);
    }

    ahb_use(ahb, ahb_devmem, devmem);
    rc = ast_ahb_access(name, argc, argv, ahb);
    if (rc) {
        errno = -rc;
        perror("ast_ahb_access");
        exit(EXIT_FAILURE);
    }

    cleanup = devmem_destroy(devmem);
    if (cleanup) {
        errno = -cleanup;
        perror("devmem_destroy");
        exit(EXIT_FAILURE);
    }

    return 0;
}

static int cmd_console(const char *name, int argc, char *argv[])
{
    struct suart _suart, *suart = &_suart;
    struct uart_mux _mux, *mux = &_mux;
    struct ahb _ahb, *ahb = &_ahb;
    const char *user, *pass;
    uint32_t data;
    int cleanup;
    int baud;
    int rc;

    if (argc < 5) {
        loge("Not enough arguments for console command\n");
        help(name);
        exit(EXIT_FAILURE);
    }

    if (strcmp("uart3", argv[0])) {
        loge("Console only supports host on 'uart3'\n");
        exit(EXIT_FAILURE);
    }

    if (strcmp("uart2", argv[1])) {
        loge("Console only supports BMC on uart2\n");
        exit(EXIT_FAILURE);
    }

    baud = atoi(argv[2]);

    user = argv[3];
    pass = argv[4];

    rc = ast_ahb_init(ahb, true);
    if (rc < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(name);
        } else if (rc == -ENOTSUP) {
            loge("Probes failed, cannot access BMC AHB\n");
        } else {
            errno = -rc;
            perror("ast_ahb_init");
        }
        exit(EXIT_FAILURE);
    }

    rc = uart_mux_init(mux, ahb);
    if (rc) { errno = -rc; perror("uart_mux_init"); goto ahb_cleanup; }

    logi("Routing UART3 to UART5\n");

    rc = uart_mux_route(mux, mux_obj_uart3, mux_obj_uart5);
    if (rc) { errno = -rc; perror("uart_mux_route"); goto ahb_cleanup; }

    rc = ahb_readl(ahb, 0x1e6e200c, &data);
    if (rc) { errno = -rc; perror("ahb_readl"); goto ahb_cleanup; }

    /* Clear UART3 clock stop bit */
    data &= ~(1 << 25);

    rc = ahb_writel(ahb, 0x1e6e200c, data);
    if (rc) { errno = -rc; perror("ahb_readl"); goto ahb_cleanup; }

    logi("Initialising SUART3\n");
    rc = suart_init_defaults(suart, sio_suart3);
    if (rc) { errno = -rc; perror("suart_init"); goto ahb_cleanup; }

    logi("Configuring baud rate of 115200 for BMC console\n");
    rc = suart_set_baud(suart, 115200);
    if (rc) { errno = -rc; perror("suart_set_baud"); goto suart_cleanup; }

    logi("Starting getty from BMC console\n");
    rc = suart_flush(suart, user, strlen(user));
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    rc = suart_flush(suart, "\n", 5);
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    sleep(3);

    rc = suart_flush(suart, pass, strlen(pass));
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    rc = suart_flush(suart, "\n", 8);
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    sleep(5);

    const char *run_getty = "/sbin/agetty -8 -L ttyS1 1200 xterm &\n";
    rc = suart_flush(suart, run_getty, strlen(run_getty));
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    /* We need to wait for the XMIT FIFO to clear before changing the UART
     * routing.
     *
     * TODO: Make this suck less by spinning on THRE
     */
    sleep(3);

    logi("Launched getty with: %s", run_getty);

    logi("Routing UARTs to connect UART3 with UART2\n");
    rc = uart_mux_restore(mux);
    if (rc) { errno = -rc; perror("uart_mux_restore"); goto suart_cleanup; }

    rc = uart_mux_connect(mux, mux_obj_uart3, mux_obj_uart2);
    if (rc) { errno = -rc; perror("uart_mux_connect"); goto suart_cleanup; }

    logi("Setting target baud rate of %d\n", baud);
    rc = suart_set_baud(suart, baud);
    if (rc) { errno = -rc; perror("suart_set_baud"); goto suart_cleanup; }

    rc = suart_flush(suart, "\n", 1);
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    sleep(5);

    rc = suart_flush(suart, user, strlen(user)); /* username */
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }
    rc = suart_flush(suart, "\n", 5);
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    sleep(3);

    rc = suart_flush(suart, pass, strlen(pass)); /* passowrd */
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    rc = suart_flush(suart, "\n", 1);
    if (rc) { errno = -rc; perror("suart_flush"); goto suart_cleanup; }

    rc = suart_run(suart, 0, 1);
    if (rc) { errno = -rc; perror("suart_run"); }

suart_cleanup:
    cleanup = suart_destroy(suart);
    if (cleanup) { errno = -cleanup; perror("suart_destroy"); }

    cleanup = uart_mux_restore(mux);
    if (cleanup) { errno = -cleanup; perror("suart_destroy"); }

ahb_cleanup:
    cleanup = ahb_destroy(ahb);
    if (cleanup) { errno = -cleanup; perror("ahb_destroy"); }

    return rc;
}

static int cmd_dump_firmware(struct ahb *ahb)
{
    uint32_t restore_tsr, sfc_tsr, sfc_wafcr;
    int cleanup;
    int rc;

    logi("Testing BMC SFC write filter configuration\n");
    rc = ahb_readl(ahb, 0x1e6200a4, &sfc_wafcr);
    if (rc) { errno = -rc; perror("ahb_readl"); return rc; }

    if (sfc_wafcr) {
        loge("Found write filter configuration 0x%x\n", sfc_wafcr);
        loge("BMC has selective write filtering enabled, bailing!\n");
        return -ENOTSUP;
    }

    /* Disable writes to CE0 - chip enables are swapped for alt boot */
    logi("Write-protecting BMC SFC\n");
    rc = ahb_readl(ahb, 0x1e620000, &sfc_tsr);
    if (rc) { errno = -rc; perror("ahb_readl"); return rc; }

    restore_tsr = sfc_tsr;
    sfc_tsr &= ~(1 << 16);

    rc = ahb_writel(ahb, 0x1e620000, sfc_tsr);
    if (rc) { errno = -rc; perror("ahb_writel"); goto cleanup_sfc; }

    logi("Exfiltrating BMC flash to stdout\n\n");
    rc = ahb_siphon_in(ahb, AST_G5_BMC_FLASH, BMC_FLASH_LEN, 1);
    if (rc) { errno = -rc; perror("ahb_siphon_in"); }

cleanup_sfc:
    logi("Clearing BMC SFC write protect state\n");
    cleanup = ahb_writel(ahb, 0x1e620000, restore_tsr);
    if (cleanup) { errno = -cleanup; perror("ahb_writel"); }

    return rc;
}

static int cmd_dump_ram(struct ahb *ahb)
{
    uint32_t scu_rev, sdmc_conf;
    uint32_t dram, vram, aram;
    int rc;

    /* Test BMC silicon revision to make sure we use the right memory map */
    logi("Checking ASPEED BMC silicon revision\n");
    rc = ahb_readl(ahb, 0x1e6e207c, &scu_rev);
    if (rc) { errno = -rc; perror("ahb_readl"); return rc; }

    if ((scu_rev >> 24) != 0x04) {
        loge("Unsupported BMC revision: 0x%08x\n", scu_rev);
        return -ENOTSUP;
    }

    logi("Found AST2500-family BMC\n");

    rc = ahb_readl(ahb, 0x1e6e0004, &sdmc_conf);
    if (rc) { errno = -rc; perror("ahb_readl"); return rc; }

    dram = bmc_dram_sizes[sdmc_conf & 0x03];
    vram = bmc_vram_sizes[(sdmc_conf >> 2) & 0x03];
    aram = dram - vram; /* Accessible DRAM */

    logi("%dMiB DRAM with %dMiB VRAM; dumping %dMiB (0x%x-0x%08x)\n",
         dram >> 20, vram >> 20, aram >> 20, AST_G5_DRAM,
         AST_G5_DRAM + aram - 1);

    rc = ahb_siphon_in(ahb, AST_G5_DRAM, aram, 1);
    if (rc) { errno = -rc; perror("ahb_siphon_in"); }

    return rc;
}

static int cmd_read(const char *name, int argc, char *argv[])
{
    struct ahb _ahb, *ahb = &_ahb;
    int rc, cleanup;

    if (argc < 1) {
        loge("Not enough arguments for read command\n");
        help(name);
        exit(EXIT_FAILURE);
    }

    rc = ast_ahb_from_args(ahb, argc - 1, argv + 1);
    printf("ast_ahb_from_args: 0x%x\n", rc);
    if (rc < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(name);
        } else if (rc == -ENOTSUP) {
            loge("Probes failed, cannot access BMC AHB\n");
        } else {
            errno = -rc;
            perror("ast_ahb_from_args");
        }
        exit(EXIT_FAILURE);
    }

    if (!strcmp("firmware", argv[0]))
        rc = cmd_dump_firmware(ahb);
    else if (!strcmp("ram", argv[0]))
        rc = cmd_dump_ram(ahb);
    else {
        loge("Unsupported read type '%s'", argv[0]);
        help(name);
        rc = -EINVAL;
    }

    cleanup = ahb_destroy(ahb);
    if (cleanup) { errno = -cleanup; perror("ahb_destroy"); }

    if (rc < 0)
        exit(EXIT_FAILURE);

    return 0;
}

static int cmd_write(const char *name, int argc, char *argv[])
{
    struct ahb _ahb, *ahb = &_ahb;
    struct flash_chip *chip;
    bool live = false;
    ssize_t ingress;
    struct sfc *sfc;
    int rc, cleanup;
    uint32_t phys;
    char *buf;

    if (argc < 1) {
        loge("Not enough arguments for write command\n");
        help(name);
        exit(EXIT_FAILURE);
    }

    if (strcmp("firmware", argv[0])) {
        loge("Unsupported write type '%s'", argv[0]);
        help(name);
        return -EINVAL;
    }

    /* Do option parsing for the "firmware" write type */
    while (1) {
        int option_index = 0;
        int c;

        static struct option long_options[] = {
            { "live", no_argument, NULL, 'l' },
            { },
        };

        c = getopt_long(argc, argv, "l", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'l':
                logi("BMC is live, will take actions to halt its execution\n");
                live = true;
                break;
        }
    }

    rc = ast_ahb_from_args(ahb, argc - optind, &argv[optind]);
    if (rc < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(name);
        } else if (rc == -ENOTSUP) {
            loge("Probes failed, cannot access BMC AHB\n");
        } else {
            errno = -rc;
            perror("ast_ahb_from_args");
        }
        exit(EXIT_FAILURE);
    }

    if (ahb->bridge == ahb_devmem)
        loge("I hope you know what you are doing\n");
    else if (live) {
        logi("Preventing system reset\n");
        rc = wdt_prevent_reset(ahb);
        if (rc < 0)
            goto cleanup_ahb;

        logi("Gating ARM clock\n");
        rc = clk_disable(ahb, clk_arm);
        if (rc < 0)
            goto cleanup_soc;

        logi("Configuring VUART for host Tx discard\n");
        rc = vuart_set_host_tx_discard(ahb, discard_enable);
        if (rc < 0)
            goto cleanup_clk;
    }

    logi("Initialising flash subsystem\n");
    rc = sfc_init(&sfc, ahb, SFC_TYPE_FMC);
    if (rc < 0)
        goto cleanup_vuart;

    rc = flash_init(sfc, &chip);
    if (rc < 0)
        goto cleanup_sfc;

    /* FIXME: Make this common with the sfc write implementation */
    buf = malloc(SFC_FLASH_WIN);
    if (!buf)
        goto cleanup_flash;

    logi("Writing firmware image\n");
    phys = 0;
    while ((ingress = read(0, buf, SFC_FLASH_WIN))) {
        if (ingress < 0) {
            rc = -errno;
            break;
        }

        do {
            if (ingress < SFC_FLASH_WIN) {
                loge("Unexpected ingress value: 0x%zx\n", ingress);
                goto cleanup_flash;
            }

            rc = flash_erase(chip, phys, ingress);
            if (rc < 0)
                goto cleanup_flash;

            rc = flash_write(chip, phys, buf, ingress, true);
            if (rc < 0)
                break;
        } while (rc == -EREMOTEIO); /* Miscompare */

        phys += ingress;
    }

    free(buf);

cleanup_flash:
    flash_destroy(chip);

cleanup_sfc:
    sfc_destroy(sfc);

cleanup_soc:
    if (live && !rc && ahb->bridge != ahb_devmem) {
        int64_t wait;

        logi("Performing SoC reset\n");
        wait = wdt_perform_reset(ahb);
        if (wait < 0) {
            rc = wait;
            goto cleanup_clk;
        }

        usleep(wait);
    }

cleanup_vuart:
    if (live) {
        logi("Deconfiguring VUART host Tx discard\n");
        cleanup = vuart_set_host_tx_discard(ahb, discard_enable);
        if (cleanup) { errno = -cleanup; perror("vuart_set_host_tx_discard"); }
    }

cleanup_clk:
    if (live && rc < 0) {
        logi("Ungating ARM clock\n");
        cleanup = clk_enable(ahb, clk_arm);
        if (cleanup) { errno = -cleanup; perror("clk_enable"); }
    }

cleanup_ahb:
    cleanup = ahb_cleanup(ahb);
    if (cleanup) { errno = -cleanup; perror("ahb_cleanup"); }

    if (rc < 0)
        exit(EXIT_FAILURE);

    return 0;
}

int cmd_replace(const char *name, int argc, char *argv[])
{
    struct ahb _ahb, *ahb = &_ahb;
    uint32_t scu_rev, sdmc_conf;
    uint32_t dram, vram, aram;
    size_t replace_len;
    size_t ram_cursor;
    void *win_chunk;
    void *needle;
    int cleanup;
    int rc;

    if (argc < 3) {
        loge("Not enough arguments for replace command\n");
        help(name);
        exit(EXIT_FAILURE);
    }

    if (strcmp("ram", argv[0])) {
        loge("Unsupported replace space: '%s'\n", argv[0]);
        help(name);
        exit(EXIT_FAILURE);
    }

    if (strlen(argv[1]) < strlen(argv[2])) {
        loge("REPLACE length %zd overruns MATCH length %zd, bailing\n",
             strlen(argv[1]), strlen(argv[2]));
        help(name);
        exit(EXIT_FAILURE);
    }

    win_chunk = malloc(DUMP_RAM_WIN);
    if (!win_chunk) { perror("malloc"); exit(EXIT_FAILURE); }

    rc = ast_ahb_init(ahb, true);
    if (rc < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(name);
        } else if (rc == -ENOTSUP) {
            loge("Probes failed, cannot access BMC AHB\n");
        } else {
            errno = -rc;
            perror("ast_ahb_init");
        }
        exit(EXIT_FAILURE);
    }

    /* Test BMC silicon revision to make sure we use the right memory map */
    logi("Checking ASPEED BMC silicon revision\n");
    rc = ahb_readl(ahb, 0x1e6e207c, &scu_rev);
    if (rc) { errno = -rc; perror("ahb_readl"); goto ahb_cleanup; }

    if ((scu_rev >> 24) != 0x04) {
        loge("Unsupported BMC revision: 0x%08x\n", scu_rev);
        goto ahb_cleanup;
    }

    logi("Found AST2500-family BMC\n");

    rc = ahb_readl(ahb, 0x1e6e0004, &sdmc_conf);
    if (rc) { errno = -rc; perror("ahb_readl"); goto ahb_cleanup; }

    dram = bmc_dram_sizes[sdmc_conf & 0x03];
    vram = bmc_vram_sizes[(sdmc_conf >> 2) & 0x03];
    aram = dram - vram; /* Accessible DRAM */

    replace_len = strlen(argv[1]);
    for (ram_cursor = AST_G5_DRAM;
         ram_cursor < AST_G5_DRAM + aram;
         ram_cursor += DUMP_RAM_WIN) {
        logi("Scanning BMC RAM in range 0x%08zx-0x%08zx\n",
             ram_cursor, ram_cursor + DUMP_RAM_WIN - 1);
        rc = ahb_read(ahb, ram_cursor, win_chunk, DUMP_RAM_WIN);
        if (rc < 0) {
            errno = -rc;
            perror("l2ab_read");
            break;
        } else if (rc != DUMP_RAM_WIN) {
            loge("Short read: %d\n", rc);
            break;
        }

        /* FIXME: Handle sub-strings at the right hand edge */
        needle = win_chunk;
        while ((needle = memmem(needle, win_chunk + DUMP_RAM_WIN - needle,
                                argv[1], strlen(argv[1])))) {
            logi("0x%08zx: Replacing '%s' with '%s'\n",
                 ram_cursor + (needle - win_chunk), argv[1], argv[2]);
            rc = ahb_write(ahb, ram_cursor + (needle - win_chunk), argv[2],
                           strlen(argv[2]));
            if (rc < 0) {
                errno = -rc;
                perror("l2ab_write");
                break;
            } else if (rc != strlen(argv[2])) {
                loge("Short write: %d\n", rc);
                break;
            }

            if ((needle + replace_len) > win_chunk + DUMP_RAM_WIN)
                break;

            needle += replace_len;
        }
    }

ahb_cleanup:
    cleanup = ahb_destroy(ahb);
    if (cleanup) { errno = -cleanup; perror("ahb_destroy"); }

    free(win_chunk);

    return rc;
}

static void cmd_probe_help(const char *name, int argc, char *argv[])
{
    static const char *probe_help =
        "Usage:\n"
        "%s probe --help\n"
        "%s probe --interface INTERFACE ...\n"
        "%s probe --list-interfaces\n"
        "%s probe --require <integrity|confidentiality>\n";

    printf(probe_help, name, name, name, name);
}

const char *ahb_interfaces[] = { "ilpc", "p2a", "xdma", "debug" };

static void cmd_probe_list_interfaces(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(ahb_interfaces); i++)
        printf("%s\n", ahb_interfaces[i]);
}

static bool cmd_probe_validate_interface(const char *iface)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(ahb_interfaces); i++)
        if (!strcmp(ahb_interfaces[i], iface))
            return true;

    return false;
}

static const enum log_colour cmd_probe_pr_ip_colours[] = {
    [ip_state_unknown] = colour_yellow,
    [ip_state_absent] = colour_white,
    [ip_state_enabled] = colour_red,
    [ip_state_disabled] = colour_green,
};

static void cmd_probe_pr_ip(enum ast_ip_state state, const char *fmt, ...)
{
    int fd = fileno(stdout);
    va_list args;
    int rc;

    va_start(args, fmt);
    rc = vdprintf(fd, fmt, args);
    va_end(args);
    if (rc < 0) {
        perror("vprintf");
        return;
    }

    log_highlight(fd, cmd_probe_pr_ip_colours[state], ast_ip_state_desc[state]);

    rc = write(fd, "\n", 1);
    if (rc < 0) {
        perror("write");
        return;
    }
}

static void cmd_probe_pr(bool result, const char *t, const char *f,
                         const char *fmt, ...)
{
    int fd = fileno(stdout);
    va_list args;
    int rc;

    va_start(args, fmt);
    rc = vdprintf(fd, fmt, args);
    va_end(args);
    if (rc < 0) {
        perror("vprintf");
        return;
    }

    if (result) {
        log_highlight(fd, colour_red, t);
    } else {
        log_highlight(fd, colour_green, f);
    }

    rc = write(fd, "\n", 1);
    if (rc < 0) {
        perror("write");
        return;
    }
}

static int cmd_probe_range_cmp(const void *a, const void *b)
{
    const int64_t as = (*(struct ahb_range **)a)->start;
    const int64_t bs = (*(struct ahb_range **)b)->start;

    return as - bs;
}

static void cmd_probe_sort_ranges(struct ahb_range *src,
                                  struct ahb_range **dst, size_t nmemb)
{
    int i;

    for (i = 0; i < p2ab_ranges_max; i++) {
        dst[i] = &src[i];
    }
    qsort(dst, nmemb, sizeof(src), cmd_probe_range_cmp);
}

static int cmd_probe(const char *name, int argc, char *argv[])
{
    struct ast_interfaces ifaces;
    bool pass_requirement = true;
    char *opt_interface = NULL;
    char *opt_require = NULL;
    int rc;
    int c;
    int i;

    while (1) {
        int option_index = 0;

        static struct option long_options[] = {
            { "help", no_argument, NULL, 'h' },
            { "interface", required_argument, NULL, 'i' },
            { "list-interfaces", no_argument, NULL, 'l' },
            { "require", required_argument, NULL, 'r' },
            { },
        };

        c = getopt_long(argc, argv, "hi:lr:", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'h':
                cmd_probe_help(name, argc, argv);
                rc = EXIT_SUCCESS;
                goto done;
            case 'i':
                if (!cmd_probe_validate_interface(optarg)) {
                    loge("Unrecognised interface: %s\n", optarg);
                    loge("Valid interfaces:\n");
                    cmd_probe_list_interfaces();
                    rc = EXIT_FAILURE;
                    goto done;
                }
                opt_interface = optarg;
                break;
            case 'l':
                cmd_probe_list_interfaces();
                rc = EXIT_SUCCESS;
                goto done;
            case 'r':
            {
                bool require_integrity = !strcmp("integrity", optarg);
                bool require_c13y = !strcmp("confidentiality", optarg);
                if (!(require_integrity || require_c13y)) {
                    loge("Unrecognised requirement: %s\n", optarg);
                    loge("Valid requirements:\n"
                                    "integrity\n"
                                    "confidentiality\n");
                    rc = EXIT_FAILURE;
                    goto done;
                }
                opt_require = optarg;
                break;
            }
        }
    }

    if (argc == optind) {
        rc = ast_ahb_bridge_probe(&ifaces);
        if (rc < 0) {
            bool denied = (rc == -EACCES || rc == -EPERM);
            if (denied && !priv_am_root()) {
                priv_print_unprivileged(name);
                rc = EXIT_FAILURE;
            } else if (rc == -ENOTSUP) {
                if (opt_require) {
                    logi("Probes failed, cannot access BMC AHB\n");
                    rc = EXIT_SUCCESS;
                } else {
                    loge("Probes failed, cannot access BMC AHB\n");
                    rc = EXIT_FAILURE;
                }
            } else {
                errno = -rc;
                perror("ast_ahb_bridge_probe");
                rc = EXIT_FAILURE;
            }
            goto done;
        }
    } else {
        struct ahb _ahb, *ahb = &_ahb;

        rc = ahb_init(ahb, ahb_debug, argv[optind + 0], argv[optind + 1],
                      strtoul(argv[optind + 2], NULL, 0), argv[optind + 3],
                      argv[optind + 4]);
        if (rc) {
            errno = -rc;
            perror("ahb_init");
            rc = EXIT_FAILURE;
            goto done;
        }

        rc = ast_ahb_bridge_discover(ahb, &ifaces);

        ahb_destroy(ahb);

        if (rc) {
            errno = -rc;
            perror("ast_ahb_bridge_discover");
            rc = EXIT_FAILURE;
            goto done;
        }
    }

    if (!opt_interface || !strcmp("ilpc", opt_interface)) {
        cmd_probe_pr_ip(ifaces.lpc.superio, "SuperIO: ");
        if (opt_require && !strcmp("confidentiality", opt_require))
            pass_requirement &= !(ifaces.lpc.superio == ip_state_enabled);

        if (ifaces.lpc.superio == ip_state_enabled) {
            cmd_probe_pr(ifaces.lpc.ilpc.rw, "Read-write", "Read-only",
                         "iLPC2AHB Bridge: ");
            if (opt_require && !strcmp("integrity", opt_require))
                pass_requirement &= !ifaces.lpc.ilpc.rw;
        }
    }
    if (!opt_interface
            || !strcmp("p2a", opt_interface)
            || !strcmp("xdma", opt_interface)) {
        cmd_probe_pr_ip(ifaces.pci.vga, "VGA PCIe device: ");
        if (ifaces.pci.vga == ip_state_enabled) {
            if (!opt_interface || !strcmp("p2a", opt_interface)) {
                cmd_probe_pr_ip(ifaces.pci.vga_mmio, "MMIO on VGA device: ");
                if (opt_require && !strcmp("confidentiality", opt_require))
                    pass_requirement &=
                        !(ifaces.pci.vga_mmio == ip_state_enabled);
                if (ifaces.pci.vga_mmio == ip_state_enabled) {
                    struct ahb_range *ranges[p2ab_ranges_max];

                    cmd_probe_sort_ranges(&ifaces.pci.ranges[0], ranges,
                                          p2ab_ranges_max);
                    printf("P2A write filter state:\n");
                    for (i = 0; i < p2ab_ranges_max; i++) {
                        struct ahb_range *r = ranges[i];

                        if (r->start == 0 && r->len == 0)
                            continue;

                        cmd_probe_pr(r->rw, "Read-write", "Read-only",
                                     "0x%08x-0x%08"PRIx64" (%s): ",
                                     r->start, (r->start + r->len - 1),
                                     r->name);

                        if (opt_require && !strcmp("integrity", opt_require))
                            pass_requirement &= !r->rw;
                    }
                }
            }
            if (!opt_interface || !strcmp("xdma", opt_interface))
                cmd_probe_pr_ip(ifaces.pci.vga_xdma, "X-DMA on VGA device: ");
        }
        cmd_probe_pr_ip(ifaces.pci.bmc, "BMC PCIe device: ");
        if (ifaces.pci.bmc == ip_state_enabled) {
            if (!opt_interface || !strcmp("p2a", opt_interface))
                cmd_probe_pr_ip(ifaces.pci.bmc_mmio, "MMIO on BMC device: ");
            if (!opt_interface || !strcmp("xdma", opt_interface))
                cmd_probe_pr_ip(ifaces.pci.bmc_xdma, "X-DMA on BMC device: ");
        }
        if ((ifaces.pci.vga == ip_state_enabled &&
                    ifaces.pci.vga_xdma == ip_state_enabled) ||
                (ifaces.pci.bmc == ip_state_enabled
                    && ifaces.pci.bmc_xdma == ip_state_enabled)) {
            if (!opt_interface || !strcmp("xdma", opt_interface))
                cmd_probe_pr(ifaces.xdma.unconstrained, "Yes", "No",
                             "X-DMA is unconstrained: ");
            if (opt_require)
                pass_requirement &= !ifaces.xdma.unconstrained;
        }
    }
    if (!opt_interface || !strcmp("debug", opt_interface)) {
        cmd_probe_pr_ip(ifaces.uart.debug, "Debug UART: ");
        if (opt_require)
            pass_requirement &= !(ifaces.uart.debug == ip_state_enabled);
        if (ifaces.uart.debug == ip_state_enabled) {
            printf("Debug UART enabled on: %s\n",
                   (ifaces.uart.uart == debug_uart1) ? "UART1" : "UART5");
        }
    }

    if (opt_require)
        rc = pass_requirement ? EXIT_SUCCESS : EXIT_FAILURE;
    else
        rc = EXIT_SUCCESS;

done:
    exit(rc);
}

static int cmd_reset(const char *name, int argc, char *argv[])
{
    struct ahb _ahb, *ahb = &_ahb;
    int64_t wait = 0;
    int cleanup;
    int rc;

    if (argc < 1) {
        loge("Not enough arguments for reset command\n");
        help(name);
        exit(EXIT_FAILURE);
    }

    if (strcmp("soc", argv[0])) {
        loge("Unsupported reset type: '%s'\n", argv[0]);
        help(name);
        exit(EXIT_FAILURE);
    }

    rc = ast_ahb_from_args(ahb, argc - 1, argv + 1);
    if (rc < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(name);
        } else if (rc == -ENOTSUP) {
            loge("Probes failed, cannot access BMC AHB\n");
        } else {
            errno = -rc;
            perror("ast_ahb_from_args");
        }
        exit(EXIT_FAILURE);
    }

    if (ahb->bridge != ahb_devmem) {
        logi("Gating ARM clock\n");
        rc = clk_disable(ahb, clk_arm);
        if (rc < 0)
            goto cleanup_ahb;
    }

    logi("Preventing system reset\n");
    rc = wdt_prevent_reset(ahb);
    if (rc < 0)
        goto cleanup_clk;

    logi("Performing SoC reset\n");
    wait = wdt_perform_reset(ahb);
    if (wait < 0) {
cleanup_clk:
        logi("Ungating ARM clock\n");
        rc = clk_enable(ahb, clk_arm);
    }

cleanup_ahb:
    cleanup = ahb_cleanup(ahb);
    if (cleanup < 0) { errno = -cleanup; perror("ahb_destroy"); }

    if (wait)
        usleep(wait);

    return rc;
}

enum flash_op { flash_op_read, flash_op_write, flash_op_erase };

static int cmd_sfc(const char *name, int argc, char *argv[])
{
    struct ahb _ahb, *ahb = &_ahb;
    struct flash_chip *chip;
    uint32_t offset, len;
    enum flash_op op;
    struct sfc *sfc;
    int rc, cleanup;
    char *buf;

    if (argc < 4) {
        loge("Not enough arguments for sfc command\n");
        help(name);
        exit(EXIT_FAILURE);
    }

    if (strcmp("fmc", argv[0])) {
        loge("Unsupported sfc type: '%s'\n", argv[0]);
        help(name);
        exit(EXIT_FAILURE);
    }

    if (!strcmp("read", argv[1])) {
        op = flash_op_read;
    } else if (!strcmp("write", argv[1])) {
        op = flash_op_write;
    } else if (!strcmp("erase", argv[1])) {
        op = flash_op_erase;
    } else {
        loge("Unsupported sfc operation: '%s'\n", argv[1]);
        help(name);
        exit(EXIT_FAILURE);
    }

    offset = strtoul(argv[2], NULL, 0);
    len = strtoul(argv[3], NULL, 0);

    rc = ast_ahb_from_args(ahb, argc - 4, argv + 4);
    if (rc < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(name);
        } else if (rc == -ENOTSUP) {
            loge("Probes failed, cannot access BMC AHB\n");
        } else {
            errno = -rc;
            perror("ast_ahb_from_args");
        }
        exit(EXIT_FAILURE);
    }

    rc = sfc_init(&sfc, ahb, SFC_TYPE_FMC);
    if (rc < 0)
        goto cleanup_ahb;

    rc = flash_init(sfc, &chip);
    if (rc < 0)
        goto cleanup_sfc;

    if (op == flash_op_read) {
        ssize_t egress;

        buf = malloc(len);
        if (!buf)
            goto cleanup_flash;

        rc = flash_read(chip, offset, buf, len);
        egress = write(1, buf, len);
        if (egress == -1) {
            rc = -errno;
            perror("write");
        }

        free(buf);
    } else if (op == flash_op_write) {
        ssize_t ingress;

        len = SFC_FLASH_WIN;
        buf = malloc(len);
        if (!buf)
            goto cleanup_flash;

        while ((ingress = read(0, buf, len))) {
            if (ingress < 0) {
                rc = -errno;
                break;
            }

            rc = flash_write(chip, offset, buf, ingress, true);
            if (rc < 0)
                break;

            offset += ingress;
        }

        free(buf);
    } else if (op == flash_op_erase) {
        rc = flash_erase(chip, offset, len);
    }

cleanup_flash:
    flash_destroy(chip);

cleanup_sfc:
    sfc_destroy(sfc);

cleanup_ahb:
    cleanup = ahb_destroy(ahb);
    if (cleanup < 0) { errno = -cleanup; perror("ahb_destroy"); }

    return rc;
}

int cmd_otp(const char *name, int argc, char *argv[])
{
    enum otp_region reg = otp_region_conf;
    struct ahb _ahb, *ahb = &_ahb;
    struct otp _otp, *otp = &_otp;
    bool rd = true;
    int argo = 2;
    int cleanup;
    int rc;

    if (argc < 2) {
        loge("Not enough arguments for otp command\n");
        help(name);
        exit(EXIT_FAILURE);
    }

    if (!strcmp("conf", argv[1]))
        reg = otp_region_conf;
    else if (!strcmp("strap", argv[1]))
        reg = otp_region_strap;
    else {
        loge("Unsupported otp region: %s\n", argv[1]);
        help(name);
        exit(EXIT_FAILURE);
    }

    if (!strcmp("write", argv[0])) {
        rd = false;
        argo += 2;
    } else if (strcmp("read", argv[0])) {
        loge("Unsupported command: %s\n", argv[0]);
        help(name);
        exit(EXIT_FAILURE);
    }

    if (argc < argo) {
        loge("Not enough arguments for otp command\n");
        help(name);
        exit(EXIT_FAILURE);
    }

    rc = ast_ahb_from_args(ahb, argc - argo, &argv[argo]);
    if (rc < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(name);
        } else if (rc == -ENOTSUP) {
            loge("Probes failed, cannot access BMC AHB\n");
        } else {
            errno = -rc;
            perror("ast_ahb_from_args");
        }
        exit(EXIT_FAILURE);
    }

    rc = otp_init(otp, ahb);
    if (rc < 0) {
        errno = -rc;
        perror("otp_init");
        cleanup = ahb_destroy(ahb);
        exit(EXIT_FAILURE);
    }

    if (rd)
        rc = otp_read(otp, reg);
    else {
        if (reg == otp_region_strap) {
            unsigned int bit;
            unsigned int val;

            bit = strtoul(argv[2], NULL, 0);
            val = strtoul(argv[3], NULL, 0);

            rc = otp_write_strap(otp, bit, val);
        } else {
            unsigned int word;
            unsigned int bit;

            word = strtoul(argv[2], NULL, 0);
            bit = strtoul(argv[3], NULL, 0);

            rc = otp_write_conf(otp, word, bit);
        }
    }

    cleanup = ahb_destroy(ahb);
    if (cleanup < 0) { errno = -cleanup; perror("ahb_destroy"); }

    return rc;
}

struct command {
    const char *name;
    int (*fn)(const char *, int, char *[]);
};

static const struct command cmds[] = {
    { "ilpc", cmd_ilpc },
    { "p2a", cmd_p2a },
    { "console", cmd_console },
    { "read", cmd_read },
    { "write", cmd_write },
    { "replace", cmd_replace },
    { "probe", cmd_probe },
    { "debug", cmd_debug },
    { "reset", cmd_reset },
    { "devmem", cmd_devmem },
    { "sfc", cmd_sfc },
    { "otp", cmd_otp },
    { },
};

int main(int argc, char *argv[])
{
    const struct command *cmd = &cmds[0];
    bool show_help = false;
    int verbose = 0;

    while (1) {
        static struct option long_options[] = {
            { "help", no_argument, NULL, 'h' },
            { "verbose", no_argument, NULL, 'v' },
            { },
        };
        int option_index = 0;
        int c;

        c = getopt_long(argc, argv, "+hv", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'h':
                show_help = true;
                break;
            case 'v':
                verbose++;
                break;
            default:
                continue;
        }
    }

    if (optind == argc) {
        if (!show_help)
            loge("Not enough arguments\n");
        help(argv[0]);
        exit(EXIT_FAILURE);
    }

    if ((level_info + verbose) <= level_trace) {
        log_set_level(level_info + verbose);
    } else {
        log_set_level(level_trace);
    }

    while (cmd->fn) {
        if (!strcmp(cmd->name, argv[optind])) {
            int offset = optind;

            /* probe uses getopt, but for subcommands not using getopt */
            if (strcmp("probe", argv[optind])) {
                offset += 1;
            }
            optind = 1;

            return cmd->fn(argv[0], argc - offset, argv + offset);
        }

        cmd++;
    }

    loge("Unknown command: %s\n", argv[1]);
    exit(EXIT_FAILURE);
}
