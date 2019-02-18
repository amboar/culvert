// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include "log.h"
#include "priv.h"

#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

bool priv_am_root(void)
{
    return !geteuid();
}

void priv_print_unprivileged(const char *name)
{
    loge("%s needs to be run as root\n", name);
}
