#include "ad9226.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TIM_HandleTypeDef htim1 = {81U, 41U, 0U};
DMA_HandleTypeDef hdma_tim1_up;
GPIO_TypeDef test_gpiof;
static uint16_t input_frame[AD9226_SAMPLE_COUNT];
static unsigned failures;

extern void HAL_DMA_XferCpltCallback(DMA_HandleTypeDef *dma);

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{
    (void)timer;
    (void)channel;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_DMA_Start_IT(DMA_HandleTypeDef *dma, uint32_t source,
                                  uint32_t destination, uint32_t count)
{
    (void)dma;
    (void)source;
    (void)destination;
    if (count != AD9226_SAMPLE_COUNT) return HAL_ERROR;
    /* The DMA substitute writes the backing buffer, as the peripheral does. */
    memcpy((void *)AD9226_GetBuf(), input_frame, sizeof(input_frame));
    return HAL_OK;
}

HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *dma)
{
    (void)dma;
    return HAL_OK;
}

uint16_t EsChooseTimerDivider(uint32_t frequency_hz)
{
    (void)frequency_hz;
    return 28U;
}

void Error_Handler(void)
{
    abort();
}

static void check(bool condition, const char *message)
{
    if (!condition)
    {
        if (failures < 8U) fprintf(stderr, "FAIL %s\n", message);
        ++failures;
    }
}

static void check_frame(const char *name)
{
    AD9226_Start();
    check(!AD9226_IsDone(), "DMA completion must be explicit");
    check(!AD9226_FinalizeFrame(), "unfinished frames must not be published");
    HAL_DMA_XferCpltCallback(&hdma_tim1_up);
    check(AD9226_IsDone(), "completed DMA frame must be available");
    check(AD9226_FinalizeFrame(), "every complete 12-bit frame must be accepted");
    for (uint32_t i = 0U; i < AD9226_SAMPLE_COUNT; ++i)
    {
        check(AD9226_GetBuf()[i] == (input_frame[i] & 0x0fffU), name);
    }
    check(AD9226_FinalizeFrame(), "finalizing twice must succeed");
    for (uint32_t i = 0U; i < AD9226_SAMPLE_COUNT; ++i)
    {
        check(AD9226_GetBuf()[i] == (input_frame[i] & 0x0fffU), "finalizing twice must preserve all samples");
    }
}

int main(void)
{
    AD9226_Init();
    for (uint32_t i = 0U; i < AD9226_SAMPLE_COUNT; ++i) input_frame[i] = (uint16_t)i;
    check_frame("0..4095 must retain every ADC bit and DC level");
    for (uint32_t i = 0U; i < AD9226_SAMPLE_COUNT; ++i)
        input_frame[i] = (uint16_t)(0xf000U | ((i * 997U) & 0x0fffU));
    check_frame("only GPIO bits 12..15 may be discarded");
    if (failures != 0U)
    {
        fprintf(stderr, "ad9226_raw_capture_tests: %u failures\n", failures);
        return 1;
    }
    puts("ad9226_raw_capture_tests: PASS (production driver, full 12-bit preservation)");
    return 0;
}
