#include "ad9226_f407.h"

static bool Ad9226F407_ConfigIsComplete(const Ad9226F407_Config *config)
{
    return (config->timer != NULL) &&
           (config->dma != NULL) &&
           (config->data_port != NULL) &&
           (config->sample_buffer != NULL) &&
           (config->sample_count != 0U);
}

Ad9226F407_Status Ad9226F407_Init(Ad9226F407 *device,
                                   const Ad9226F407_Config *config)
{
    if ((device == NULL) || (config == NULL) || !Ad9226F407_ConfigIsComplete(config))
    {
        return AD9226_F407_ERROR_ARGUMENT;
    }

    if ((config->timer_clock_hz == 0U) ||
        (config->min_sample_rate_hz == 0U) ||
        (config->max_sample_rate_hz < config->min_sample_rate_hz))
    {
        return AD9226_F407_ERROR_RANGE;
    }

    /* 已初始化对象必须先 Stop，活动帧绝不能被重新初始化覆盖。 */
    if (device->initialized || device->busy || device->recovery_pending)
    {
        return AD9226_F407_ERROR_STATE;
    }

    if (HAL_TIM_PWM_Start(config->timer,
                          config->timer_channel) != HAL_OK)
    {
        /* 初始 PWM 失败时不改写调用方对象或其他硬件状态。 */
        return AD9226_F407_ERROR_HAL;
    }

    device->config = *config;
    device->initialized = true;
    device->busy = false;
    device->done = false;
    device->recovery_pending = false;
    device->actual_sample_rate_hz = 0U;
    return AD9226_F407_OK;
}

Ad9226F407_Status Ad9226F407_AbortFrame(Ad9226F407 *device)
{
    if (device == NULL)
    {
        return AD9226_F407_ERROR_ARGUMENT;
    }

    if (!device->initialized)
    {
        return AD9226_F407_ERROR_STATE;
    }

    if (!device->busy && !device->recovery_pending)
    {
        return AD9226_F407_OK;
    }

    /* 先阻止定时器继续发出请求，再在任务上下文等待 HAL 中止 DMA。 */
    __HAL_TIM_DISABLE_DMA(device->config.timer,
                          device->config.timer_dma_request);
    device->busy = false;
    device->done = false;
    device->recovery_pending = true;
    if (HAL_DMA_Abort(device->config.dma) != HAL_OK)
    {
        return AD9226_F407_ERROR_HAL;
    }

    device->recovery_pending = false;
    return AD9226_F407_OK;
}

Ad9226F407_Status Ad9226F407_Stop(Ad9226F407 *device)
{
    Ad9226F407_Status status;

    if (device == NULL)
    {
        return AD9226_F407_ERROR_ARGUMENT;
    }

    if (!device->initialized)
    {
        return AD9226_F407_ERROR_STATE;
    }

    status = Ad9226F407_AbortFrame(device);
    if (status != AD9226_F407_OK)
    {
        return status;
    }

    /* Stop 是任务上下文 API，负责在重新 Init 前关闭 PWM。 */
    __HAL_TIM_DISABLE_DMA(device->config.timer,
                          device->config.timer_dma_request);
    if (HAL_TIM_PWM_Stop(device->config.timer,
                         device->config.timer_channel) != HAL_OK)
    {
        return AD9226_F407_ERROR_HAL;
    }

    device->initialized = false;
    device->busy = false;
    device->done = false;
    device->recovery_pending = false;
    device->actual_sample_rate_hz = 0U;
    return AD9226_F407_OK;
}

Ad9226F407_Status Ad9226F407_Start(Ad9226F407 *device)
{
    if (device == NULL)
    {
        return AD9226_F407_ERROR_ARGUMENT;
    }

    if (!device->initialized || device->busy || device->recovery_pending)
    {
        return AD9226_F407_ERROR_STATE;
    }

    /* 新一帧开始前不再暴露旧帧数据。 */
    device->busy = false;
    device->done = false;
    if (HAL_DMA_Start_IT(device->config.dma,
                         (uint32_t)(uintptr_t)&device->config.data_port->IDR,
                         (uint32_t)(uintptr_t)device->config.sample_buffer,
                         (uint32_t)device->config.sample_count) != HAL_OK)
    {
        return AD9226_F407_ERROR_HAL;
    }

    /* 先发布忙碌状态，避免使能请求时的即时完成中断被忽略。 */
    device->busy = true;
    __HAL_TIM_ENABLE_DMA(device->config.timer,
                         device->config.timer_dma_request);
    return AD9226_F407_OK;
}

bool Ad9226F407_OnDmaComplete(Ad9226F407 *device, DMA_HandleTypeDef *dma)
{
    if ((device == NULL) || (dma == NULL) || !device->busy ||
        device->recovery_pending || (dma != device->config.dma))
    {
        return false;
    }

    __HAL_TIM_DISABLE_DMA(device->config.timer,
                          device->config.timer_dma_request);
    device->busy = false;
    device->done = true;
    return true;
}

bool Ad9226F407_OnDmaError(Ad9226F407 *device, DMA_HandleTypeDef *dma)
{
    if ((device == NULL) || (dma == NULL) || !device->busy ||
        (dma != device->config.dma))
    {
        return false;
    }

    /* DMA 错误回调可能处于 ISR；这里只做无阻塞的请求关断和状态发布。 */
    __HAL_TIM_DISABLE_DMA(device->config.timer,
                          device->config.timer_dma_request);
    device->busy = false;
    device->done = false;
    device->recovery_pending = true;
    return true;
}

bool Ad9226F407_IsDone(const Ad9226F407 *device)
{
    return (device != NULL) && device->done;
}

const uint16_t *Ad9226F407_GetSamples(const Ad9226F407 *device,
                                      size_t *sample_count)
{
    if (sample_count != NULL)
    {
        *sample_count = 0U;
    }

    if ((device == NULL) || !device->done)
    {
        return NULL;
    }

    if (sample_count != NULL)
    {
        *sample_count = device->config.sample_count;
    }
    return device->config.sample_buffer;
}

Ad9226F407_Status Ad9226F407_SetSampleRate(Ad9226F407 *device,
                                            uint32_t requested_hz,
                                            uint32_t *actual_hz)
{
    uint64_t rounded_clock_hz;
    uint32_t divider;
    uint32_t arr;
    uint32_t compare;
    uint32_t calculated_actual_hz;

    if ((device == NULL) || (actual_hz == NULL))
    {
        return AD9226_F407_ERROR_ARGUMENT;
    }

    if (!device->initialized || device->busy || device->recovery_pending)
    {
        return AD9226_F407_ERROR_STATE;
    }

    if ((requested_hz < device->config.min_sample_rate_hz) ||
        (requested_hz > device->config.max_sample_rate_hz))
    {
        return AD9226_F407_ERROR_RANGE;
    }

    rounded_clock_hz = (uint64_t)device->config.timer_clock_hz +
                       ((uint64_t)requested_hz / 2U);
    divider = (uint32_t)(rounded_clock_hz / requested_hz);
    if ((divider == 0U) || (divider > 65536U))
    {
        return AD9226_F407_ERROR_RANGE;
    }

    arr = divider - 1U;
    compare = (divider / 2U > 0U) ? (divider / 2U - 1U) : 0U;
    calculated_actual_hz = device->config.timer_clock_hz / divider;

    __HAL_TIM_SET_AUTORELOAD(device->config.timer, arr);
    __HAL_TIM_SET_COMPARE(device->config.timer,
                          device->config.timer_channel,
                          compare);
    device->actual_sample_rate_hz = calculated_actual_hz;
    *actual_hz = calculated_actual_hz;
    return AD9226_F407_OK;
}
