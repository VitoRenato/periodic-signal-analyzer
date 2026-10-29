#ifndef TEST_H743_HAL_H
#define TEST_H743_HAL_H

#include <stdint.h>

typedef enum
{
    HAL_OK = 0x00U,
    HAL_ERROR = 0x01U,
    HAL_BUSY = 0x02U,
    HAL_TIMEOUT = 0x03U
} HAL_StatusTypeDef;

typedef struct
{
    uint32_t unused;
} SPI_HandleTypeDef;

typedef enum
{
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET
} GPIO_PinState;

uint32_t HAL_GetTick(void);
void Error_Handler(void);

#endif
