// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
#include <string.h>
#include <stdlib.h>

#include "ahb.h"
#include "ast.h"
#include "log.h"
#include "debug.h"

int cmd_debug(const char *name, int argc, char *argv[])
{
    struct debug _debug, *debug = &_debug;
    struct ahb _ahb, *ahb = &_ahb;
    int rc, cleanup;

    /* ./doit debug read 0x1e6e207c digi,portserver-ts-16 <IP> <SERIAL PORT> <USER> <PASSWORD> */
    if (!argc) {
        loge("Not enough arguments for debug command\n");
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
            exit(EXIT_FAILURE);
        }
    } else {
        loge("Unsupported command: %s\n", argv[0]);
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
