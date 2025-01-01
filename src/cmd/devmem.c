// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "ahb.h"
#include "ast.h"
#include "bridge/devmem.h"
#include "cmd.h"
#include "priv.h"

static int do_devmem(const char *name, int argc, char *argv[])
{
    struct devmem _devmem, *devmem = &_devmem;
    struct ahb *ahb;
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

    ahb = devmem_as_ahb(devmem);
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

static const struct cmd devmem_cmd = {
    "devmem",
    "<read ADDRESS|write ADDRESS VALUE>",
    do_devmem,
};
REGISTER_CMD(devmem_cmd);
