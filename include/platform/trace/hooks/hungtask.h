/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hungtask

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH platform/trace/hooks

#if !defined(_TRACE_HUNGTASK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HUNGTASK_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(set_did_panic,
	TP_PROTO(int did_panic),
	TP_ARGS(did_panic));

DECLARE_HOOK(set_timeout_secs,
	TP_PROTO(int timeout_sec),
	TP_ARGS(timeout_sec));

DECLARE_RESTRICTED_HOOK(check_tasks,
	TP_PROTO(unsigned long ts),
	TP_ARGS(ts), 1);

DECLARE_HOOK(mmap_sem_debug,
	TP_PROTO(const struct rw_semaphore *sem),
	TP_ARGS(sem));

#endif /* _TRACE_HUNGTASK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
