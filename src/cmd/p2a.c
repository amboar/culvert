// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ahb.h"
#include "ast.h"
#include "log.h"
#include "p2a.h"
#include "priv.h"

int cmd_p2a(const char *name, int argc, char *argv[])
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
