/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _PCI_H
#define _PCI_H

#include <stdint.h>

int pci_open(uint16_t vid, uint16_t did, int bar);

int pci_close(int fd);

#endif
