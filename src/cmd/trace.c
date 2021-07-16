// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
// Copyright (C) 2021, Oracle and/or its affiliates.

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ahb.h"
#include "ast.h"
#include "log.h"
#include "priv.h"
#include "soc.h"
#include "trace.h"

//doit trace ADDRESS WIDTH:OFFSET MODE
//doit trace 0x1e788000 1:0 read
//doit trace 0x1e788000 2:2 read
//doit trace 0x1e788000 4:0 write
int cmd_trace(const char *name, int argc, char *argv[])
{
    struct trace _trace, *trace = &_trace;
    struct ahb _ahb, *ahb = &_ahb;
    struct soc _soc, *soc = &_soc;
    enum trace_mode mode;
    uint32_t addr, width;
    sigset_t set;
    int sig;
    int rc;

    if (argc < 3) {
        loge("Not enough arguments for trace command\n");
        return -EINVAL;
    }

    addr = strtoul(argv[0], NULL, 0);
    width = strtoul(argv[1], NULL, 0);
    if (width != 1 && width != 2 && width != 4) {
        loge("invalid access size\n");
        return -EINVAL;
    }

    if (addr & (width - 1)) {
        loge("listening address must be aligned to the access size\n");
        return -EINVAL;
    }

    if (!strcmp("read", argv[2])) {
        mode = trace_read;
    } else if (!strcmp("write", argv[2])) {
        mode = trace_write;
    } else {
        loge("Unrecognised trace mode: %s\n", argv[2]);
        return -EINVAL;
    }

    if (sigemptyset(&set)) {
        rc = -errno;
        loge("Unable to initialise signal set: %d\n", rc);
        return rc;
    }

    if (sigaddset(&set, SIGINT)) {
        rc = -errno;
        loge("Unable to add SIGINT to signal set: %d\n", rc);
        return rc;
    }

    if ((rc = ast_ahb_from_args(ahb, argc - 3, &argv[3])) < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(name);
        } else if (rc == -ENOTSUP) {
            loge("Probes failed, cannot access BMC AHB\n");
        } else {
            errno = -rc;
            perror("ast_ahb_from_args");
        }
        return rc;
    }

    ;
    if ((rc = soc_probe(soc, ahb)) < 0)
        goto cleanup_ahb;

    if ((rc = trace_init(trace, soc))) {
        loge("Unable to initialise trace object\n");
        goto cleanup_soc;
    }

    if ((rc = trace_start(trace, addr, width, mode))) {
        loge("Unable to start trace for 0x%08x %db %s: %d\n",
             addr, width, mode, rc);
        goto cleanup_soc;
    }

    sigprocmask(SIG_BLOCK, &set, NULL);
    if ((rc = sigwait(&set, &sig))) {
        rc = -rc;
        loge("Unable to wait for SIGINT: %d\n", rc);
        goto cleanup_soc;
    }

    if ((rc = trace_stop(trace))) {
        loge("Unable to stop trace: %d\n");
        goto cleanup_soc;
    }

    if ((rc = trace_dump(trace, 1)))
        loge("Unable to dump trace to stdout: %d\n", rc);

cleanup_soc:
    soc_destroy(soc);

cleanup_ahb:
    ahb_destroy(ahb);

    return rc;
}
