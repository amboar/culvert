/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 Jackson Bopp */

#ifndef _LOCK_H
#define _LOCK_H

/*
 * Acquire an exclusive, non-blocking advisory lock scoped to `name`,
 * preventing another culvert instance from concurrently using the same
 * named resource (e.g. a bridge driver). Returns an open file descriptor
 * on success, or a negative errno if the lock is already held elsewhere
 * or could not be acquired.
 */
int lock_acquire(const char *name);

/* Release a lock previously acquired with lock_acquire(). No-op if fd < 0. */
void lock_release(int fd);

#endif
