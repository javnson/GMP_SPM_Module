#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include "stm32f4xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

void LedControl_Init(TIM_HandleTypeDef *htim);
void LedControl_SetSystem(bool fault_active);
void LedControl_SetLed5(bool input_valid, bool charging, bool manual_test);
void LedControl_SetSupercap(uint32_t sc_mv);

#endif /* LED_CONTROL_H */
