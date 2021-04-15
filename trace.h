/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _TRACE_H
#define _TRACE_H

enum trace_mode { trace_read = 0, trace_write };

int trace_start(struct ahb *ahb, uint32_t addr, int width, int offset,
		enum trace_mode mode);
int trace_stop(struct ahb *ahb);
int trace_dump(struct ahb *ahb, int outfd);

#endif
