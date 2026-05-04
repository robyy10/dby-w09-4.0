
#ifndef __PMIC_CPU_LOG_H_
#define __PMIC_CPU_LOG_H_

#ifdef CONFIG_QTI_PMIC_PON_LOG

int save_cpu_info(u8 cpu_id);

#else

static inline int save_cpu_info(u8 cpu_id)
{
	return 0;
}

#endif

#endif
