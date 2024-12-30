/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2024 Andrew Jeffery */

#ifndef CULVERT_CMD_H
#define CULVERT_CMD_H

#include "ccan/autodata/autodata.h"
#include <string.h>

struct cmd {
    const char *name;
    const char *help;
    int (*fn)(const char *, int, char *[]);
};

AUTODATA_TYPE(cmds, struct cmd);
#define REGISTER_CMD(cmd) AUTODATA_SYM(cmds, cmd);

static inline int cmd_cmp(const void *a, const void *b)
{
    const struct cmd * const *acmd = a;
    const struct cmd * const *bcmd = b;

    return strcmp((*acmd)->name, (*bcmd)->name);
}

#endif
