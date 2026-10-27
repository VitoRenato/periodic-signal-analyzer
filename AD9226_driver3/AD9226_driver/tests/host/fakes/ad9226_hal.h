#ifndef AD9226_TEST_HAL_H
#define AD9226_TEST_HAL_H
#define __MAIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t ARR; uint32_t CCR1; uint32_t requests; } TIM_HandleTypeDef;
typedef struct { uint32_t unused; } DMA_HandleTypeDef;
typedef struct { uint32_t IDR; } GPIO_TypeDef;
extern GPIO_TypeDef test_gpiof;
#define GPIOF (&test_gpiof)
#define TIM_CHANNEL_1 1U
#define TIM_DMA_UPDATE 1U
#define __HAL_TIM_GET_AUTORELOAD(timer) ((timer)->ARR)
#define __HAL_TIM_SET_AUTORELOAD(timer, value) ((timer)->ARR = (value))
#define __HAL_TIM_SET_COMPARE(timer, channel, value) ((void)(channel), (timer)->CCR1 = (value))
#define __HAL_TIM_ENABLE_DMA(timer, mask) ((timer)->requests |= (mask))
#define __HAL_TIM_DISABLE_DMA(timer, mask) ((timer)->requests &= ~(mask))
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel);
HAL_StatusTypeDef HAL_DMA_Start_IT(DMA_HandleTypeDef *dma, uint32_t source, uint32_t destination, uint32_t count);
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *dma);
void Error_Handler(void);

#endif
