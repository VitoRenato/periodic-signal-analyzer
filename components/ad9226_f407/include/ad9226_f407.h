#ifndef AD9226_F407_H
#define AD9226_F407_H

#include "ad9226_f407_config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include AD9226_F407_HAL_HEADER

typedef enum
{
    AD9226_F407_OK = 0,
    AD9226_F407_ERROR_ARGUMENT,
    AD9226_F407_ERROR_STATE,
    AD9226_F407_ERROR_HAL,
    AD9226_F407_ERROR_RANGE
} Ad9226F407_Status;

typedef struct
{
    TIM_HandleTypeDef *timer;
    DMA_HandleTypeDef *dma;
    GPIO_TypeDef *data_port;
    uint32_t timer_channel;
    uint32_t timer_dma_request;
    uint32_t timer_clock_hz;
    uint32_t min_sample_rate_hz;
    uint32_t max_sample_rate_hz;
    uint16_t *sample_buffer;
    size_t sample_count;
} Ad9226F407_Config;

typedef struct
{
    Ad9226F407_Config config;
    volatile bool initialized;
    volatile bool busy;
    volatile bool done;
    /* DMA 出错或中止失败后，必须先在任务上下文完成 AbortFrame。 */
    volatile bool recovery_pending;
    uint32_t actual_sample_rate_hz;
} Ad9226F407;

Ad9226F407_Status Ad9226F407_Init(Ad9226F407 *device,
                                   const Ad9226F407_Config *config);
/* 仅可在任务上下文调用；停止 PWM 并释放该对象的初始化状态。 */
Ad9226F407_Status Ad9226F407_Stop(Ad9226F407 *device);
Ad9226F407_Status Ad9226F407_Start(Ad9226F407 *device);
/* 仅可在任务上下文调用；中止当前或待恢复的 DMA 帧。 */
Ad9226F407_Status Ad9226F407_AbortFrame(Ad9226F407 *device);
bool Ad9226F407_OnDmaComplete(Ad9226F407 *device, DMA_HandleTypeDef *dma);
/* 可在 DMA 错误回调中调用；不执行可能阻塞的 HAL DMA 中止。 */
bool Ad9226F407_OnDmaError(Ad9226F407 *device, DMA_HandleTypeDef *dma);
bool Ad9226F407_IsDone(const Ad9226F407 *device);
const uint16_t *Ad9226F407_GetSamples(const Ad9226F407 *device,
                                      size_t *sample_count);
Ad9226F407_Status Ad9226F407_SetSampleRate(Ad9226F407 *device,
                                            uint32_t requested_hz,
                                            uint32_t *actual_hz);

#endif
