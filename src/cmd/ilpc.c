// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
#include <stdio.h>
#include <stdlib.h>

#include "ahb.h"
#include "ast.h"
#include "bridge/ilpc.h"
#include "cmd.h"
#include "priv.h"

static int do_ilpc(const char *name, int argc, char *argv[])
{
    struct ilpcb _ilpcb, *ilpcb = &_ilpcb;
    struct ahb *ahb;
    int cleanup;
    int rc;

    rc = ilpcb_init(ilpcb);
    if (rc < 0) {
        bool denied = (rc == -EACCES || rc == -EPERM);
        if (denied && !priv_am_root()) {
            priv_print_unprivileged(name);
        } else {
            errno = -rc;
            perror("ilpcb_init");
        }
        exit(EXIT_FAILURE);
    }

    ahb = ilpcb_as_ahb(ilpcb);
    rc = ast_ahb_access(name, argc, argv, ahb);
    if (rc) {
        errno = -rc;
        perror("ast_ahb_access");
        exit(EXIT_FAILURE);
    }

    cleanup = ilpcb_destroy(ilpcb);
    if (cleanup) {
        errno = -cleanup;
        perror("ilpcb_destroy");
        exit(EXIT_FAILURE);
    }

    return 0;
}

static const struct cmd ilpc_cmd = {
    "ilpc",
    "<read ADDRESS|write ADDRESS VALUE",
    do_ilpc,
};
REGISTER_CMD(ilpc_cmd);
