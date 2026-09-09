// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Jackson Bopp

#include "lock.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/file.h>
#include <unistd.h>

#define LOCK_DIR "/run/lock"

int lock_acquire(const char *name)
{
	char path[64];
	int fd;
	int rc;

	rc = snprintf(path, sizeof(path), "%s/culvert.%s", LOCK_DIR, name);
	if (rc < 0 || (size_t)rc >= sizeof(path)) {
		loge("Bridge name '%s' is too long to form a lock path\n",
		     name);
		return -ENAMETOOLONG;
	}

	fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0644);
	if (fd < 0) {
		rc = -errno;
		loge("Failed to open lock file '%s': %d\n", path, rc);
		return rc;
	}

	if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
		rc = -errno;
		if (rc == -EWOULDBLOCK) {
			loge("Bridge '%s' is already in use by another culvert instance\n",
			     name);
		} else {
			loge("Failed to lock '%s': %d\n", path, rc);
		}
		close(fd);
		return rc;
	}

	return fd;
}

void lock_release(int fd)
{
	if (fd < 0)
		return;

	flock(fd, LOCK_UN);
	close(fd);
}
