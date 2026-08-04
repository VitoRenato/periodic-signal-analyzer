#ifndef STM32F4XX_HAL_H
#define STM32F4XX_HAL_H

#include <stdint.h>

typedef enum
{
    HAL_OK = 0,
    HAL_ERROR = 1
} HAL_StatusTypeDef;

typedef struct
{
    uint32_t ARR;
    uint32_t CCR;
    uint32_t enabled_dma_request;
    uint32_t disabled_dma_request;
} TIM_TypeDef;

typedef struct
{
    TIM_TypeDef *Instance;
} TIM_HandleTypeDef;

typedef struct DMA_HandleTypeDef DMA_HandleTypeDef;

typedef void (*pDMA_CallbackTypeDef)(DMA_HandleTypeDef *hdma);

typedef enum
{
    HAL_DMA_XFER_CPLT_CB_ID = 0U,
    HAL_DMA_XFER_ERROR_CB_ID = 1U
} HAL_DMA_CallbackIDTypeDef;

struct DMA_HandleTypeDef
{
    uint32_t marker;
    pDMA_CallbackTypeDef XferCpltCallback;
    pDMA_CallbackTypeDef XferErrorCallback;
};

typedef struct
{
    volatile uint32_t IDR;
} GPIO_TypeDef;

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *timer, uint32_t channel);
HAL_StatusTypeDef HAL_DMA_Start_IT(DMA_HandleTypeDef *dma,
                                   uint32_t source,
                                   uint32_t destination,
                                   uint32_t length);
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *dma);
HAL_StatusTypeDef HAL_DMA_RegisterCallback(DMA_HandleTypeDef *dma,
                                            HAL_DMA_CallbackIDTypeDef callback_id,
                                            pDMA_CallbackTypeDef callback);
void FakeTim_EnableDma(TIM_HandleTypeDef *timer, uint32_t request);
void FakeTim_DisableDma(TIM_HandleTypeDef *timer, uint32_t request);
void FakeTim_SetAutoreload(TIM_HandleTypeDef *timer, uint32_t value);
void FakeTim_SetCompare(TIM_HandleTypeDef *timer, uint32_t channel, uint32_t value);

#define __HAL_TIM_ENABLE_DMA(timer, request) FakeTim_EnableDma((timer), (request))
#define __HAL_TIM_DISABLE_DMA(timer, request) FakeTim_DisableDma((timer), (request))
#define __HAL_TIM_SET_AUTORELOAD(timer, value) FakeTim_SetAutoreload((timer), (value))
#define __HAL_TIM_SET_COMPARE(timer, channel, value) \
    FakeTim_SetCompare((timer), (channel), (value))

#define USE_HAL_DMA_REGISTER_CALLBACKS 1U

#endif
