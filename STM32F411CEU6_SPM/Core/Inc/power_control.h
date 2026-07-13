#ifndef POWER_CONTROL_H
#define POWER_CONTROL_H

#include "stm32f4xx_hal.h"

typedef struct
{
  uint32_t input_mv;
  uint32_t bus_mv;
  uint32_t sc_mv;
  uint32_t sc_energy_percent;
  uint32_t faults;
  uint32_t mode;
  uint32_t input_valid;
  uint32_t boost_enabled;
  uint32_t charge_requested;
  uint32_t charge_enabled;
  uint32_t output_enabled;
  uint32_t boost_forced_off;
  uint32_t output_forced_off;
  uint32_t startup_complete;
  uint32_t alarm_muted;
  uint32_t low_energy_warning;
  uint32_t buzzer_mode;
} PowerDebugSnapshot;

extern volatile PowerDebugSnapshot g_power_debug;

void PowerControl_Init(ADC_HandleTypeDef *hadc, I2C_HandleTypeDef *hi2c,
                       TIM_HandleTypeDef *htim_led);
void PowerControl_Run(void);

#endif /* POWER_CONTROL_H */
