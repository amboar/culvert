// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#define STDOUT 1
#define STDERR 2

ssize_t shell_get_output(const char *cmd, char *buf, size_t len)
{
        int _stdout, _stderr;
        size_t remaining;
        ssize_t ingress;
        int cleanup;
        ssize_t rc;
        int p[2];

        assert(cmd);
        assert(buf);
        assert(len);

        _stdout = dup(STDOUT);
        if (_stdout == -1)
                return -errno;

        _stderr = dup(STDERR);
        if (_stderr == -1) {
                rc = -errno;
                goto cleanup_stdout_dup;
        }

        rc = close(STDERR);
        if (rc == -1) {
                rc = -errno;
                goto cleanup_stderr_dup;
        }

        rc = open("/dev/null", O_WRONLY);
        if (rc == -1) {
                rc = -errno;
                goto cleanup_stderr_close;
        }

        rc = pipe(p);
        if (rc == -1) {
                rc = -errno;
                goto cleanup_stderr_close;
        }

        rc = dup2(p[1], STDOUT);
        if (rc == -1) {
                rc = -errno;
                goto cleanup_pipe;
        }

        assert(rc == STDOUT);

        errno = 0;
        rc = system(cmd);
        if (rc == -1) {
                assert(errno);
                rc = -errno;
                goto cleanup_stdout;
        }

        close(STDOUT);
        close(p[1]);
        p[1] = -1;

        remaining = len;
        do {
                ingress = read(p[0], buf, remaining);
                if (ingress == -1) {
                        rc = -errno;
                        goto cleanup_stdout;
                }

                buf += ingress;
                remaining -= ingress;
        } while (ingress && remaining);

        if (remaining)
                *buf = '\0';
        else
                *(buf - 1) = '\0';

        rc = len - remaining;

cleanup_stdout:
        cleanup = dup2(_stdout, STDOUT);
        if (cleanup != STDOUT)
                perror("dup2");
cleanup_pipe:
        if (p[1] != -1)
                close(p[1]);
        close(p[0]);

cleanup_stderr_close:
        dup2(_stderr, STDERR);

cleanup_stderr_dup:
        close(_stderr);

cleanup_stdout_dup:
        close(_stdout);

        return rc;
}
