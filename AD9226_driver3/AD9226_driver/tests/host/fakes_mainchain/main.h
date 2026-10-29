#ifndef TEST_MAINCHAIN_MAIN_H
#define TEST_MAINCHAIN_MAIN_H

#include "stm32f4xx_hal.h"

void Error_Handler(void);
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);
void HMI_RxByte(uint8_t byte);

#endif
