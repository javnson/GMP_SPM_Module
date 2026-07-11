#ifndef POWER_CONTROL_H
#define POWER_CONTROL_H

#include "stm32f4xx_hal.h"

void PowerControl_Init(ADC_HandleTypeDef *hadc, I2C_HandleTypeDef *hi2c);
void PowerControl_Run(void);

#endif /* POWER_CONTROL_H */
