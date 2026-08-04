/*
 * AD9226 F407 社区驱动的最小 CubeMX/HAL 集成示例。
 * 调用方持有采样缓冲区与驱动对象，不在中断中处理整帧数据。
 */
#include "ad9226_f407.h"

#define APP_AD9226_SAMPLE_RATE_HZ 2000000U

#if defined(AD9226_EXAMPLE_HOST_BUILD)
/* 主机检查使用测试 fake HAL，替代 CubeMX 生成的全局句柄和 GPIOC 宏。 */
static TIM_TypeDef g_ad9226_host_tim1_instance;
static GPIO_TypeDef g_ad9226_host_gpioc;
static TIM_HandleTypeDef htim1 = { .Instance = &g_ad9226_host_tim1_instance };
static DMA_HandleTypeDef hdma_tim1_up;

#define GPIOC (&g_ad9226_host_gpioc)
#define TIM_CHANNEL_1 0U
#define TIM_DMA_UPDATE 0U
#else
/* CubeMX 生成的句柄应与实际 DMA stream/通道保持一致。 */
extern TIM_HandleTypeDef htim1;
extern DMA_HandleTypeDef hdma_tim1_up;
#endif

static uint16_t g_ad9226_samples[4096];
static Ad9226F407 g_ad9226;
static uint32_t g_ad9226_actual_sample_rate_hz;
static volatile bool g_ad9226_frame_pending;
static volatile bool g_ad9226_recovery_pending;

static const Ad9226F407_Config g_ad9226_config = {
    .timer = &htim1,
    .dma = &hdma_tim1_up,
    .data_port = GPIOC,
    .timer_channel = TIM_CHANNEL_1,
    .timer_dma_request = TIM_DMA_UPDATE,
    .timer_clock_hz = 168000000U,
    .min_sample_rate_hz = 5000U,
    .max_sample_rate_hz = 20000000U,
    .sample_buffer = g_ad9226_samples,
    .sample_count = 4096U
};

/* 由应用实现；在非中断上下文中处理 samples 更合适。 */
extern void App_Ad9226SamplesReady(const uint16_t *samples, size_t sample_count);

static void App_Ad9226DmaCompleteCallback(DMA_HandleTypeDef *hdma)
{
    if (Ad9226F407_OnDmaComplete(&g_ad9226, hdma))
    {
        /* 中断只发布完成标志，整帧读取和处理留给主循环。 */
        g_ad9226_frame_pending = true;
    }
}

static void App_Ad9226DmaErrorCallback(DMA_HandleTypeDef *hdma)
{
    if (Ad9226F407_OnDmaError(&g_ad9226, hdma))
    {
        /* 中断不执行 HAL_DMA_Abort；由主循环调用 App_Ad9226Recover。 */
        g_ad9226_recovery_pending = true;
    }
}

static Ad9226F407_Status App_Ad9226RegisterDmaCompleteCallback(void)
{
#if defined(USE_HAL_DMA_REGISTER_CALLBACKS) && (USE_HAL_DMA_REGISTER_CALLBACKS == 1U)
    if (HAL_DMA_RegisterCallback(&hdma_tim1_up,
                                 HAL_DMA_XFER_CPLT_CB_ID,
                                 App_Ad9226DmaCompleteCallback) != HAL_OK)
    {
        return AD9226_F407_ERROR_HAL;
    }
    if (HAL_DMA_RegisterCallback(&hdma_tim1_up,
                                 HAL_DMA_XFER_ERROR_CB_ID,
                                 App_Ad9226DmaErrorCallback) != HAL_OK)
    {
        return AD9226_F407_ERROR_HAL;
    }
#else
    /* 旧版 HAL 没有注册 API 时，直接绑定同一 DMA 句柄的完成回调。 */
    hdma_tim1_up.XferCpltCallback = App_Ad9226DmaCompleteCallback;
    hdma_tim1_up.XferErrorCallback = App_Ad9226DmaErrorCallback;
#endif
    return AD9226_F407_OK;
}

Ad9226F407_Status App_Ad9226Init(void)
{
    Ad9226F407_Status status;

    status = Ad9226F407_Init(&g_ad9226, &g_ad9226_config);
    if (status != AD9226_F407_OK)
    {
        return status;
    }

    status = App_Ad9226RegisterDmaCompleteCallback();
    if (status != AD9226_F407_OK)
    {
        /* 回调未就绪时不留下已启动的 PWM/驱动对象。 */
        (void)Ad9226F407_Stop(&g_ad9226);
        return status;
    }

    if (status == AD9226_F407_OK)
    {
        g_ad9226_frame_pending = false;
        g_ad9226_recovery_pending = false;
    }
    return status;
}

Ad9226F407_Status App_Ad9226Start(void)
{
    Ad9226F407_Status status;

    if (g_ad9226_frame_pending || g_ad9226_recovery_pending)
    {
        return AD9226_F407_ERROR_STATE;
    }

    status = Ad9226F407_SetSampleRate(&g_ad9226,
                                      APP_AD9226_SAMPLE_RATE_HZ,
                                      &g_ad9226_actual_sample_rate_hz);
    if (status != AD9226_F407_OK)
    {
        return status;
    }

    return Ad9226F407_Start(&g_ad9226);
}

/* 在主循环调用；DMA 错误回调后完成可能阻塞的 HAL DMA 中止。 */
Ad9226F407_Status App_Ad9226Recover(void)
{
    Ad9226F407_Status status;

    if (!g_ad9226_recovery_pending)
    {
        return AD9226_F407_ERROR_STATE;
    }

    status = Ad9226F407_AbortFrame(&g_ad9226);
    if (status == AD9226_F407_OK)
    {
        g_ad9226_recovery_pending = false;
    }
    return status;
}

bool App_Ad9226Poll(void)
{
    const uint16_t *samples;
    size_t sample_count;

    if (!g_ad9226_frame_pending)
    {
        return false;
    }

    /* 在开始整帧处理前领取该帧，重复中断不会重复交付。 */
    g_ad9226_frame_pending = false;
    samples = Ad9226F407_GetSamples(&g_ad9226, &sample_count);
    if (samples != NULL)
    {
        App_Ad9226SamplesReady(samples, sample_count);
        return true;
    }
    return false;
}
