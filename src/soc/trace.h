/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2018,2019 IBM Corp. */
/* Copyright (C) 2021, Oracle and/or its affiliates. */

#ifndef _TRACE_H
#define _TRACE_H

#include "soc.h"

enum trace_mode { trace_read = 0, trace_write };

struct trace;

int trace_init(struct trace *ctx, struct soc *soc);
int trace_start(struct trace *ctx, uint32_t addr, int width,
		enum trace_mode mode);
int trace_stop(struct trace *ctx);
int trace_dump(struct trace *ctx, int outfd);

struct trace *trace_get(struct soc *soc);

#endif
