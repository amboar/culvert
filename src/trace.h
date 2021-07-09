/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */

#ifndef _TRACE_H
#define _TRACE_H

#include "soc.h"

struct trace {
	struct soc *soc;
	struct soc_region ahbc;
	struct soc_region sram;
};

enum trace_mode { trace_read = 0, trace_write };

int trace_init(struct trace *ctx, struct soc *soc);
int trace_start(struct trace *ctx, uint32_t addr, int width, int offset,
		enum trace_mode mode);
int trace_stop(struct trace *ctx);
int trace_dump(struct trace *ctx, int outfd);

#endif
