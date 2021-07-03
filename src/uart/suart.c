// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "suart.h"
#include "log.h"

#define UART_RBR 0x00
#define UART_THR 0x00
#define UART_DLL 0x00
#define UART_IER 0x01
#define UART_DLH 0x01
#define UART_IIR 0x02
#define UART_FCR 0x02
#define   UART_FCR_XMIT_RST (1 << 2)
#define   UART_FCR_RCVR_RST (1 << 1)
#define   UART_FCR_FIFO_EN  (1 << 0)
#define UART_LCR 0x03
#define   UART_LCR_DLAB     (1 << 7)
#define   UART_LCR_BREAK    (1 << 6)
#define   UART_LCR_EPS      (1 << 4)
#define   UART_LCR_PEN      (1 << 3)
#define   UART_LCR_CLS_MASK 0x03
#define   UART_LCR_CLS_8    0x03
#define UART_MCR 0x04
#define   UART_MCR_LOOP     (1 << 4)
#define   UART_MCR_OUT2     (1 << 3)
#define   UART_MCR_OUT1     (1 << 2)
#define   UART_MCR_NRTS     (1 << 1)
#define   UART_MCR_NDTR     (1 << 0)
#define UART_LSR 0x05
#define   UART_LSR_ERROR    (1 << 7)
#define   UART_LSR_TEMT     (1 << 6)
#define   UART_LSR_THRE     (1 << 5)
#define   UART_LSR_BI       (1 << 4)
#define   UART_LSR_FE       (1 << 3)
#define   UART_LSR_PE       (1 << 2)
#define   UART_LSR_OE       (1 << 1)
#define   UART_LSR_DR       (1 << 0)
#define UART_MSR 0x06
#define   UART_MSR_CTS_N    (1 << 4)
#define UART_SCR 0x07

#define UART_DEFAULT_BAUD 115200

static inline uint16_t baud_to_divisor(int baud)
{
    /* Divide by 13 can get in the bin. So much debugging */
    return ((24000000 / 13) / (16 * baud));
}

static int __suart_init(struct suart *ctx, enum sio_dev dev, bool defaults,
                        uint16_t base, int sirq)
{
    struct sio _sio, *sio = &_sio;
    struct lpc *io = &ctx->io;
    uint16_t divisor;
    uint8_t data;
    int cleanup;
    int rc;

    switch (dev) {
        case sio_suart1:
        case sio_suart2:
        case sio_suart3:
        case sio_suart4:
            break;
        default:
            return -EINVAL;
    }


    rc = sio_init(sio);
    if (rc)
        return rc;

    rc = sio_unlock(sio);
    if (rc)
        return rc;

    rc = sio_select(sio, dev);
    if (rc)
        goto sio_err;

    if (defaults) {
        /* Grab the SUART's LPC base address, 16 bits in two byte-size registers */
        rc = sio_readb(sio, 0x60, &data);
        if (rc)
            goto sio_err;

        ctx->base = data << 8;

        rc = sio_readb(sio, 0x61, &data);
        if (rc)
            goto sio_err;

        ctx->base |= data;

        rc = sio_readb(sio, 0x70, &data);
        if (rc)
            goto sio_err;

        ctx->sirq = data;
    } else {
        rc = sio_writeb(sio, 0x60, base >> 8);
        if (rc)
            goto sio_err;

        rc = sio_writeb(sio, 0x61, base & 0xff);
        if (rc)
            goto sio_err;

        ctx->base = base;

        rc = sio_writeb(sio, 0x70, sirq);
        if (rc)
            goto sio_err;

        ctx->sirq = sirq;
    }

    logd("SUART base address: 0x%x\n", ctx->base);
    logd("SUART SIRQ: %d\n", ctx->sirq);

    /* Enable the SUART */
    rc = sio_writeb(sio, 0x30, 1);
    if (rc)
        goto sio_err;

    rc = sio_lock(sio);
    if (rc) {
        errno = -rc;
        perror("sio_lock");
    }

    rc = sio_destroy(sio);
    if (rc)
        return rc;

    /* Init LPC */
    rc = lpc_init(io, "io");
    if (rc)
        return rc;

    /* Disable interrupts, will be polling */
    rc = lpc_writeb(io, ctx->base + UART_IER, 0);
    if (rc)
        goto cleanup_lpc;

    /* Setup Loop/DTR/RTS signal control */
    rc = lpc_writeb(io, ctx->base + UART_MCR,
                    (UART_MCR_OUT2 | UART_MCR_NRTS | UART_MCR_NDTR));
    if (rc)
        goto cleanup_lpc;

    /* Configure 115200 8N1 */
    divisor = baud_to_divisor(UART_DEFAULT_BAUD);
    rc = lpc_writeb(io, ctx->base + UART_LCR,
                    (UART_LCR_DLAB | UART_LCR_EPS | UART_LCR_CLS_8));
    if (rc)
        goto cleanup_lpc;

    rc = lpc_writeb(io, ctx->base + UART_DLH, (uint8_t)(divisor >> 8));
    if (rc)
        goto cleanup_lpc;

    rc = lpc_writeb(io, ctx->base + UART_DLL, divisor & 0xff);
    if (rc)
        goto cleanup_lpc;

    rc = lpc_writeb(io, ctx->base + UART_LCR, (UART_LCR_EPS | UART_LCR_CLS_8));
    if (rc)
        goto cleanup_lpc;

    /* Polled FIFO Mode */
    return lpc_writeb(io, ctx->base + UART_FCR,
                      (UART_FCR_XMIT_RST | UART_FCR_RCVR_RST | UART_FCR_FIFO_EN));

cleanup_lpc:
    cleanup = lpc_destroy(io);
    if (cleanup) {
        errno = -cleanup;
        perror("lpc_destroy");
    }

    return rc;

sio_err:
    cleanup = sio_lock(sio);
    if (cleanup) {
        errno = -cleanup;
        perror("sio_lock");
    }

    cleanup = sio_destroy(sio);
    if (cleanup) {
        errno = -cleanup;
        perror("sio_destroy");
    }

    return rc;
}

int suart_init_defaults(struct suart *ctx, enum sio_dev dev)
{
    return __suart_init(ctx, dev, true, 0, 0);
}

int suart_init(struct suart *ctx, enum sio_dev dev, uint16_t base, int sirq)
{
    return __suart_init(ctx, dev, false, base, sirq);
}

int suart_destroy(struct suart *ctx)
{
    struct sio _sio, *sio = &_sio;
    int cleanup;
    int rc;

    rc = sio_init(sio);
    if (rc)
        return rc;

    rc = sio_unlock(sio);
    if (rc)
        return rc;

    rc = sio_select(sio, ctx->dev);
    if (rc)
        goto sio_err;

    /* Disable the SUART */
    rc = sio_writeb(sio, 0x30, 0);
    if (rc)
        goto sio_err;

sio_err:
    cleanup = sio_lock(sio);
    if (cleanup) {
        errno = -cleanup;
        perror("sio_lock");
    }

    cleanup = sio_destroy(sio);
    if (cleanup) {
        errno = -cleanup;
        perror("sio_destroy");
    }

    return rc;
}

int suart_set_baud(struct suart *ctx, int rate)
{
    struct lpc *io = &ctx->io;
    uint16_t divisor;
    uint8_t lcr, lsr;
    int rc;

    rc = lpc_readb(io, ctx->base + UART_LCR, &lcr);
    if (rc)
        return rc;

    rc = lpc_writeb(io, ctx->base + UART_LCR, (lcr | UART_LCR_DLAB));
    if (rc)
        return rc;

    divisor = baud_to_divisor(rate);
    rc = lpc_writeb(io, ctx->base + UART_DLH, (uint8_t)(divisor >> 8));
    if (rc)
        return rc;

    rc = lpc_writeb(io, ctx->base + UART_DLL, divisor & 0xff);
    if (rc)
        return rc;

    rc = lpc_writeb(io, ctx->base + UART_LCR, (lcr & ~UART_LCR_DLAB));
    if (rc)
        return rc;

    /* Reset the FIFOs to ensure any baud rate weirdness is gone */
    rc = lpc_writeb(&ctx->io, ctx->base + UART_FCR,
                    (UART_FCR_RCVR_RST | UART_FCR_XMIT_RST | UART_FCR_FIFO_EN));
    if (rc)
        return rc;

    rc = lpc_readb(io, ctx->base + UART_LSR, &lsr);
    if (rc)
        return rc;

    if (lsr & UART_LSR_ERROR)
        loge("Found error state after FIFO reset: 0x%x\n", lsr);

    ctx->baud = rate;

    return 0;
}

ssize_t suart_write(struct suart *ctx, const char *buf, size_t len)
{
    struct lpc *io = &ctx->io;
    size_t slots = 16;
    uint8_t lsr;
    int rc;

    if (!len)
        return len;

    rc = lpc_readb(io, ctx->base + UART_LSR, &lsr);
    if (rc)
        return rc;

    if (lsr & UART_LSR_ERROR) {
        loge("Error condition asserted: 0x%x\n", lsr);
        if (lsr & UART_LSR_BI)
            loge("Break condition asserted\n");
        if (lsr & UART_LSR_FE)
            loge("Framing error condition asserted\n");
        if (lsr & UART_LSR_PE)
            loge("Parity error condition asserted\n");
        return -EIO;
    }

    if (lsr & UART_LSR_OE)
        loge("Overrun condition asserted\n");

    if (lsr & UART_LSR_DR) {
        /* We want to go read RBR ASAP */
        return len;
    }

    if (!(lsr & UART_LSR_THRE))
        return len;

    while (len && slots) {
        rc = lpc_writeb(io, ctx->base + UART_THR, *buf++);
        if (rc)
            return rc;

        len--;
        slots--;
    }

    return len;
}

ssize_t suart_read(struct suart *ctx, char *buf, size_t len)
{
    struct lpc *io = &ctx->io;
    char *end = buf + len;
    uint8_t lsr;
    int retry;
    int rc;

    rc = lpc_readb(io, ctx->base + UART_LSR, &lsr);
    if (rc)
        return rc;

    if (lsr & UART_LSR_ERROR) {
        loge("Error condition asserted: 0x%x\n", lsr);
        if (lsr & UART_LSR_BI)
            loge("Break condition asserted\n");
        if (lsr & UART_LSR_FE)
            loge("Framing error condition asserted\n");
        if (lsr & UART_LSR_PE)
            loge("Parity error condition asserted\n");
        return -EIO;
    }

    if (lsr & UART_LSR_OE)
        loge("Overrun condition asserted\n");

    if (!(lsr & UART_LSR_DR))
        return 0;

    retry = 100;
    do {
        while (!(rc = lpc_readb(io, ctx->base + UART_LSR, &lsr)) &&
                 (lsr & UART_LSR_DR) && buf < end) {
            rc = lpc_readb(io, ctx->base + UART_RBR, (uint8_t *)buf++);
            if (rc)
                return rc;
        }
    } while (--retry);


    if (rc)
        return rc;

    return buf - (end - len);
}

/*
 * uin: UART input from the host side to send to the BMC
 * uout: UART output from the BMC to send to the Host
 */
int suart_run(struct suart *ctx, int uin, int uout)
{
    char _uout_buf[1024], *uout_buf;
    char _uin_buf[16], *uin_buf;
    struct epoll_event epevent;
    int epfd;
    int rc;

    epevent.events = EPOLLIN | EPOLLERR;
    epevent.data.fd = uin;

    epfd = epoll_create1(0);
    rc = epoll_ctl(epfd, EPOLL_CTL_ADD, uin, &epevent);
    if (rc == -1)
        return -errno;

    while ((rc = epoll_wait(epfd, &epevent, 1, 50)) != -1) {
        if (rc) {
            const char *uin_end;
            ssize_t uin_write;
            ssize_t uin_read;

            uin_buf = &_uin_buf[0];
            assert(rc == 1);

            uin_read = read(uin, uin_buf, sizeof(_uin_buf));
            if (uin_read == -1)
                return -errno;

            uin_end = uin_buf + uin_read;
            uin_write = uin_read;
            while (uin_read > 0) {
                ssize_t uout_wrote;
                ssize_t uout_read;

                uin_write = suart_write(ctx, uin_end - uin_write, uin_write);
                if (uin_write < 0)
                    return uin_write;

                uout_buf = &_uout_buf[0];
                uout_read = suart_read(ctx, uout_buf, sizeof(_uout_buf));
                if (uout_read < 0)
                    return uout_read;

                while (uout_read > 0) {
                    uout_wrote = write(uout, uout_buf, uout_read);
                    if (uout_wrote == -1)
                        return -errno;

                    uout_read -= uout_wrote;
                    uout_buf += uout_wrote;
                }

                uin_read = uin_write;
            }
        } else {
            ssize_t uout_wrote;
            ssize_t uout_read;

            uout_buf = &_uout_buf[0];
            uout_read = suart_read(ctx, uout_buf, sizeof(_uout_buf));
            if (uout_read < 0)
                return uout_read;

            while (uout_read > 0) {
                uout_wrote = write(uout, uout_buf, uout_read);
                if (uout_wrote == -1)
                    return -errno;

                uout_read -= uout_wrote;
                uout_buf += uout_wrote;
            }
        }
    }

    if (rc == -1)
        return -errno;

    return 0;
}

ssize_t suart_flush(struct suart *ctx, const char *buf, size_t len)
{
    const char *end = buf + len;
    ssize_t remaining = len;
    int rc;

    while (remaining > 0) {
        /* Force a reset of the RCVR FIFO, we're flushing XMIT */
        rc = lpc_writeb(&ctx->io, ctx->base + UART_FCR,
                        (UART_FCR_RCVR_RST | UART_FCR_FIFO_EN));
        if (rc)
            return rc;

        remaining = suart_write(ctx, end - remaining, remaining);
    }

    return remaining;
}

ssize_t suart_fill(struct suart *ctx, char *buf, size_t len)
{
    ssize_t remaining = len;
    ssize_t consumed = 0;

    while (remaining) {
        consumed = suart_read(ctx, buf, remaining);
        if (consumed < 0)
            return consumed;

        remaining -= consumed;
        buf += consumed;

        sleep(1);
    }

    return len - remaining;
}

ssize_t suart_fill_until(struct suart *ctx, char *buf, size_t len, char term)
{
    ssize_t remaining;
    ssize_t consumed = 0;

    if (len > SSIZE_MAX)
        return -EINVAL;

    /* Condition is broken when reading bursts of multiple characters */
    while ((remaining < (ssize_t)len && *(buf - 1) != term) || remaining) {
        consumed = suart_read(ctx, buf, remaining);
        if (consumed < 0)
            return consumed;

        remaining -= consumed;
        buf += consumed;

        sleep(1);
    }

    return len - remaining;
}
