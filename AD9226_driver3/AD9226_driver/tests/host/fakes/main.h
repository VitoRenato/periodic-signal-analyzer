#ifndef TEST_FAKE_MAIN_H
#define TEST_FAKE_MAIN_H

#include <stdint.h>

typedef struct
{
  uint32_t unused;
} UART_HandleTypeDef;

typedef enum
{
  HAL_OK = 0x00U,
  HAL_ERROR = 0x01U,
  HAL_BUSY = 0x02U,
  HAL_TIMEOUT = 0x03U
} HAL_StatusTypeDef;

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart,
                                    uint8_t *data,
                                    uint16_t len,
                                    uint32_t timeout);
void HAL_Delay(uint32_t delay_ms);
uint32_t HAL_GetTick(void);

#endif /* TEST_FAKE_MAIN_H */
