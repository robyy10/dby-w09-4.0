#ifndef _ANTENNA_CABLE_ADC_DETECT
#define _ANTENNA_CABLE_ADC_DETECT

#ifdef CONFIG_RFFE_ANTENNA
int cable_out_get(void);
#else
static inline int cable_out_get(void)
{
    return 0;
}
#endif

#endif