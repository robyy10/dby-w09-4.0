/*
 * memcheck_log_ion.h
 *
 * Get ION memory info function
 *
 * Copyright (c) 2022 Huawei Technologies Co., Ltd.
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

#ifndef _MEMCHECK_LOG_ION_H
#define _MEMCHECK_LOG_ION_H

#ifndef CONFIG_ARCH_MONACO
int memcheck_ion_createfs(void);
void memcheck_ion_info_show(void);

#else /* CONFIG_ARCH_MONACO */
static inline int memcheck_ion_createfs(void)
{
	return -EINVAL;
}
static inline void memcheck_ion_info_show(void)
{
}
#endif /* CONFIG_ARCH_MONACO */
#endif /* _MEMCHECK_LOG_ION_H */
