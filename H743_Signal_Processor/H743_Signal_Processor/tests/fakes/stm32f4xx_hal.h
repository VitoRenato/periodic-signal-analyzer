#ifndef TEST_STM32F4XX_HAL_H
#define TEST_STM32F4XX_HAL_H

#include "stm32h7xx_hal.h"

typedef struct
{
    uint32_t unused;
} TIM_HandleTypeDef;

typedef struct
{
    uint32_t unused;
} GPIO_TypeDef;

extern GPIO_TypeDef test_gpioa;
#define GPIOA (&test_gpioa)
#define GPIO_PIN_15 0x8000U

#endif
