/*
 * hwcfs_rwsem.c
 *
 * rwsem schedule implementation
 *
 * Copyright (c) 2017-2020 Huawei Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#ifdef CONFIG_HW_VIP_THREAD
/*lint -save -e578 -e695 -e571*/
#include <chipset_common/hwcfs/hwcfs_rwsem.h>

#include <linux/list.h>
#include <chipset_common/hwcfs/hwcfs_common.h>

enum rwsem_waiter_type {
	RWSEM_WAITING_FOR_WRITE,
	RWSEM_WAITING_FOR_READ
};

struct rwsem_waiter {
	struct list_head list;
	struct task_struct *task;
	enum rwsem_waiter_type type;
};

#define RWSEM_READER_OWNED ((struct task_struct *)1UL)

static inline bool rwsem_owner_is_writer(struct task_struct *owner)
{
	return owner && owner != RWSEM_READER_OWNED;
}

static void rwsem_list_add_vip(struct list_head *entry, struct list_head *head)
{
	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	struct rwsem_waiter *waiter = NULL;

	list_for_each_safe(pos, n, head) {
		waiter = list_entry(pos, struct rwsem_waiter, list);
		if (!test_task_vip(waiter->task)) {
			list_add(entry, waiter->list.prev);
			return;
		}
	}
	if (pos == head)
		list_add_tail(entry, head);
}

void rwsem_list_add(struct task_struct *tsk,
	struct list_head *entry, struct list_head *head)
{
	bool is_vip = test_set_dynamic_vip(tsk);

	if (!entry || !head)
		return;

	if (is_vip)
		rwsem_list_add_vip(entry, head);
	else
		list_add_tail(entry, head);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,4,0)) && defined(CONFIG_ARM64)
#define RWSEM_OWNER_BOOST       (1UL << 6)
static bool is_rwsem_owner_boost(struct rw_semaphore *sem)
{
	return atomic_long_read(&sem->count) & RWSEM_OWNER_BOOST;
}

static void rwsem_owner_set_boost(struct rw_semaphore *sem)
{
	unsigned long count;

	count = atomic_long_read(&sem->count);
	do {
		if (count & RWSEM_OWNER_BOOST)
			break;
	} while (!atomic_long_try_cmpxchg(&sem->count, &count,
		count | RWSEM_OWNER_BOOST));
}

static void rwsem_owner_clear_boost(struct rw_semaphore *sem)
{
	unsigned long count;

	count = atomic_long_read(&sem->count);
	do {
		if (!(count & RWSEM_OWNER_BOOST))
			break;
	} while (!atomic_long_try_cmpxchg(&sem->count, &count,
		count & ~RWSEM_OWNER_BOOST));
}
#elif defined(CONFIG_ARM64)
#define RWSEM_OWNER_BOOST       (1UL << 17)
static bool is_rwsem_owner_boost(struct rw_semaphore *sem)
{
#ifdef CONFIG_RWSEM_PRIO_AWARE
	return sem->m_count & RWSEM_OWNER_BOOST;
#else
	return false;
#endif
}

static void rwsem_owner_set_boost(struct rw_semaphore *sem)
{
#ifdef CONFIG_RWSEM_PRIO_AWARE
	sem->m_count |= RWSEM_OWNER_BOOST;
#endif
}

static void rwsem_owner_clear_boost(struct rw_semaphore *sem)
{
#ifdef CONFIG_RWSEM_PRIO_AWARE
	sem->m_count &= ~RWSEM_OWNER_BOOST;
#endif
}
#endif

void rwsem_dynamic_vip_enqueue(
	struct task_struct *tsk, struct task_struct *waiter_task,
	struct task_struct *owner, struct rw_semaphore *sem)
{
	if (!waiter_task || !tsk || !sem || !rwsem_owner_is_writer(owner))
		return;
#ifdef CONFIG_ARM64
	if (is_rwsem_owner_boost(sem))
#else
	if (sem->vip_dep_task != NULL)
#endif
		return;

	if (test_task_vip(owner))
		return;
	if (!test_set_dynamic_vip(tsk)) {
#ifdef CONFIG_HW_VIP_SEMAPHORE
		if (is_rwsem_vip(sem) && owner->prio >= DEFAULT_PRIO && tsk->group_leader) {
			if (!tsk->group_leader->static_vip)
				return;
			tsk = tsk->group_leader;
		} else {
			return;
		}
#else
		return;
#endif
	}
	dynamic_vip_enqueue(owner, DYNAMIC_VIP_RWSEM, tsk->vip_depth);

#ifdef CONFIG_ARM64
	rwsem_owner_set_boost(sem);
#else
	sem->vip_dep_task = owner;
#endif
}

void rwsem_dynamic_vip_dequeue(struct rw_semaphore *sem,
	struct task_struct *tsk)
{
#ifdef CONFIG_ARM64
	if (tsk && sem && is_rwsem_owner_boost(sem)) {
		dynamic_vip_dequeue(tsk, DYNAMIC_VIP_RWSEM);
		rwsem_owner_clear_boost(sem);
	}
#else
	if (tsk && sem && sem->vip_dep_task == tsk) {
		dynamic_vip_dequeue(tsk, DYNAMIC_VIP_RWSEM);
		sem->vip_dep_task = NULL;
	}
#endif
}

/*lint -restore*/
#endif

