/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022. All rights reserved.
 * Description: radix tree leak problem debug
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Xilun Li <lixilun1@huawei.com>
 *
 * Create: 2022-11-15
 */

/*
 * Type 0(H): head pointer the first RCU callback in the list.
 * Type 1(W): RCU_DONE_TAIL, also RCU_WAIT head.
 * Type 2(R): RCU_WAIT_TAIL, also RCU_NEXT_READY head.
 * Type 3(N): RCU_NEXT_READY_TAIL, also RCU_NEXT head.
 * Type 4(T): RCU_NEXT_TAIL, call_rcu will add rcu head to this.
 */
static void show_rcu_head_content(const struct rcu_head *head, int type)
{
	unsigned long offset;
	unsigned long head_ptr;
	unsigned long next_ptr;

	if (type > RCU_CBLIST_NSEGS || type < 0)
		return;

	if (head) {
		offset = (unsigned long)head->func;
		/* Hide the true addr */
		head_ptr = (unsigned long)head & 0xFFFF;
		next_ptr = (unsigned long)(head->next) & 0xFFFF;

		/* Offset is checked to deal with kfree_rcu */
		if (__is_kfree_rcu_offset(offset))
			pr_err("     %c: h:0x%lx o:%lu n:0x%lx\n",
			       "HWRNT"[type], head_ptr, offset, next_ptr);
		else
			pr_err("     %c: h:0x%lx f:%ps n:0x%lx\n",
			       "HWRNT"[type], head_ptr, head->func, next_ptr);
	}
}

static void show_segcblist_content(struct rcu_data *rdp)
{
	int i;
	unsigned long flags;
	struct rcu_segcblist *rsclp = &rdp->cblist;

	/* Avoid race of rcu_do_batch and call_rcu */
	local_irq_save(flags);
	rcu_nocb_lock(rdp);

	/* Show rsclp head content */
	show_rcu_head_content(rsclp->head, 0);

	/* Show rsclp tail list content */
	for (i = RCU_DONE_TAIL; i < RCU_CBLIST_NSEGS; i++) {
		struct rcu_head *head = NULL;
		/* Safe here as rcu_segcblist_restempty had already read this */
		head = *READ_ONCE(rsclp->tails[i]);
		show_rcu_head_content(head, i + 1);
	}

	rcu_nocb_unlock_irqrestore(rdp, flags);
}

/*
 * Rcu segcblist corrupt will result in slab memory leak
 */
void show_rcu_gp_kthreads_debug(void)
{
	unsigned long slab_pages = global_node_page_state(NR_SLAB_UNRECLAIMABLE) +
				   global_node_page_state(NR_SLAB_RECLAIMABLE);
	/* 1000mb, 1024kb x 1024 / 4kb = 262144 */
	if (slab_pages > 262144) {
		pr_err("slab leak detected, pages:%lu", slab_pages);
		show_rcu_gp_kthreads();
	}
}

static int rcu_panic_dump(struct notifier_block *self,
			  unsigned long v, void *p)
{
	show_rcu_gp_kthreads_debug();
	return 0;
}

static struct notifier_block rcu_panic_notifier = {
	.notifier_call = rcu_panic_dump,
	.priority = 1,
};

static int __init register_rcu_panic_notifier(void)
{
	atomic_notifier_chain_register(&panic_notifier_list,
				       &rcu_panic_notifier);
	return 0;
}

late_initcall(register_rcu_panic_notifier);
