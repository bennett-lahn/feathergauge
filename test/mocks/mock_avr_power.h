#pragma once

// AVR power-reduction register macros stubbed as no-ops for native (x86/ARM) test builds.
// On the 32u4 these gate ADC and timer clocks to save power between sensor reads and sleep.

#ifndef PRTIM4
  #define PRTIM4 4
#endif

#ifndef power_timer3_disable
  #define power_timer3_disable() ((void)0)
#endif

#ifndef power_timer4_disable
  #define power_timer4_disable() ((void)0)
#endif

#ifndef power_timer4_enable
  #define power_timer4_enable()  ((void)0)
#endif

#ifndef power_adc_disable
  #define power_adc_disable()    ((void)0)
#endif

#ifndef power_adc_enable
  #define power_adc_enable()     ((void)0)
#endif
