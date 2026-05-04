/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM memcheck

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH platform/trace/hooks

#if !defined(_TRACE_MEMCHECK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MEMCHECK_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

DECLARE_RESTRICTED_HOOK(mm_mem_stats_show,
	TP_PROTO(int unused),
	TP_ARGS(unused), 1);

DECLARE_RESTRICTED_HOOK(cma_report,
	TP_PROTO(char *name, unsigned long total, unsigned long req),
	TP_ARGS(name, total, req), 1);

DECLARE_RESTRICTED_HOOK(slub_obj_report,
	TP_PROTO(struct kmem_cache *s),
	TP_ARGS(s), 1);

DECLARE_RESTRICTED_HOOK(lowmem_report,
	TP_PROTO(struct task_struct *p, unsigned long points),
	TP_ARGS(p, points), 1);

#endif /* _TRACE_MEMCHECK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
