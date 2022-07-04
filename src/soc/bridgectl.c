// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2022 IBM Corp.

#include "log.h"
#include "soc/bridgectl.h"

#include <stdio.h>
#include <unistd.h>

static const char *bridge_mode_description[] = {
    [bm_permissive] = "Permissive",
    [bm_restricted] = "Restricted",
    [bm_disabled] = "Disabled",
};

static const enum log_colour bridge_mode_colour[] = {
    [bm_permissive] = colour_red,
    [bm_restricted] = colour_yellow,
    [bm_disabled] = colour_green,
};

void bridgectl_log_status(struct bridgectl *ctx, int fd, enum bridge_mode status)
{
    int rc;

    log_highlight(fd, bridge_mode_colour[status], "%s:\t%s", bridgectl_name(ctx), bridge_mode_description[status]);

    rc = write(fd, "\n", 1);
    if (rc < 0) {
        perror("write");
        return;
    }
}
