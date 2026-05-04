/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_ARCH_MSM_WATCHDOG_H_
#define _ASM_ARCH_MSM_WATCHDOG_H_

#ifdef CONFIG_QCOM_FORCE_WDOG_BITE_ON_PANIC
#define WDOG_BITE_ON_PANIC 1
#else
#define WDOG_BITE_ON_PANIC 0
#endif

#ifdef CONFIG_QCOM_WATCHDOG_V2
void msm_trigger_wdog_bite(void);
#else
static inline void msm_trigger_wdog_bite(void) { }
#endif

#if defined(CONFIG_QCOM_WATCHDOG_V2) && defined(CONFIG_HW_SHUTDOWN_WDT)
void msm_stop_pet_wdog(void);
#else
static inline void msm_stop_pet_wdog(void) { }
#endif

#endif
