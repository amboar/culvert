// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pci.h"
#include "shell.h"

int pci_open(uint16_t vid, uint16_t did, int bar)
{
        char cmd_stdout[1024];
        struct stat statbuf;
        char *cmd, *end;
        char *cmd_fmt;
        char *res;
        int rc;
        int fd;

        cmd_fmt = "/bin/bash -c \"comm -12 "
                  "<(grep -l 0x%x /sys/bus/pci/devices/*/vendor|xargs dirname)"
                  " "
                  "<(grep -l 0x%x /sys/bus/pci/devices/*/device|xargs dirname)"
                  "\"";

        rc = asprintf(&cmd, cmd_fmt, vid, did);
        if (rc == -1)
                return -errno;

        rc = shell_get_output(cmd, &cmd_stdout[0], sizeof(cmd_stdout));
        free(cmd);
        if (rc < 0)
                return rc;

        assert(rc < sizeof(cmd_stdout));

        /* Open the first match, because ¯\_(ツ)_/¯ - assume we only have one */
        end = strchr(cmd_stdout, '\n');
        if (!end)
                return -EIO;

        *end = '\0';

        /* Just check if we found anything */
        rc = stat(cmd_stdout, &statbuf);
        if (rc < 0)
                return -errno;

        rc = asprintf(&res, "%s/resource%d", cmd_stdout, bar);
        if (rc == -1)
                return -errno;

        fd = open(res, O_RDWR | O_SYNC);
        free(res);

        return fd;
}

int pci_close(int fd)
{
        assert(fd >= 0);

        return close(fd);
}
