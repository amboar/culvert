// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2019 IBM Corp.
// Copyright (C) 2021, Oracle and/or its affiliates.

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
#include <dirent.h>

#include "pci.h"
#include "shell.h"

int read_sysfs_id(int dirfd, const char *file)
{
	char id_string[7];
	int rc, fd;
	long id;

	fd = openat(dirfd, file, 0);
	if (fd < 0)
		return -1;

	memset(id_string, 0, sizeof(id_string));

	/* vdid format is: "0xABCD" */
	rc = read(fd, id_string, 6);
	close(fd);

	if (rc < 0)
		return -1;

	id = strtol(id_string, NULL, 16);
	if (id > 0xffff || id < 0)
		return -1;
	return id;
}

int pci_open(uint16_t vid, uint16_t did, int bar)
{
        char *res;
        int rc;
        int fd;
	int found = 0;

	struct dirent *de;
	int dfd;
	DIR *d;
	char path[300]; /* de->d_name has a max of 255, and add some change */

	d = opendir("/sys/bus/pci/devices/");
	if (!d)
		return -errno;

	dfd = dirfd(d); /* we need an FD for openat() */

	while ((de = readdir(d))) {
		int this_vid, this_did;

		if (de->d_type != DT_LNK)
			continue;

		snprintf(path, sizeof(path), "%s/vendor", de->d_name);
		this_vid = read_sysfs_id(dfd, path);

		snprintf(path, sizeof(path), "%s/device", de->d_name);
		this_did = read_sysfs_id(dfd, path);

		if (this_vid == vid && this_did == did) {
			found = 1;
			break;
		}
	}

	/* NB: dfd and de->d_name are both invalidated by closedir() */
	if (!found) {
		closedir(d);
		return -ENOENT;
	}

	rc = asprintf(&res, "%s/resource%d", de->d_name, bar);
	if (rc == -1) {
		closedir(d);
		return -errno;
	}

	fd = openat(dfd, res, O_RDWR | O_SYNC);
	free(res);
	closedir(d);

        return fd;
}

int pci_close(int fd)
{
        assert(fd >= 0);

        return close(fd);
}
