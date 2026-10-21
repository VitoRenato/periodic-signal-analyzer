/**
 * @file    ad9226.c
 * @brief   AD9226 采集驱动实现: TIM1_UP 触发 DMA 读 GPIOF->IDR
 * @note    每 4096 点完成,设 done_flag,关 DMA
 */
#include "ad9226.h"
#include "main.h"
#include "equivalent_sampling.h"

/* 外部句柄 (CubeMX 在 tim.c / dma.c 里定义) */
extern TIM_HandleTypeDef  htim1;
extern DMA_HandleTypeDef  hdma_tim1_up;

/* 4096 点缓冲区,16 bit (12 bit 数据 + 4 bit padding) */
static uint16_t s_adc_buf[AD9226_SAMPLE_COUNT];
static volatile bool s_adc_done = false;
static bool s_frame_finalized = false;

/* 当前采样率 (Hz),默认值 = 168M / (0+1) / (81+1) = 2 048 780 Hz */
static volatile uint32_t s_fs_hz = 2048780UL;

/* 等效采样状态 */
static volatile bool     s_eq_mode = false;
static volatile uint32_t s_eq_f0_hz = 0UL;
static volatile uint16_t s_eq_divider_req = 0U;  /* 请求值(命令收到即写) */
static volatile uint16_t s_eq_divider_cap = 0U;  /* 帧启动时捕获的实际 divider */

void AD9226_Init(void)
{
    s_adc_done = false;
    s_frame_finalized = false;
    /* 启动 TIM1 CH1 PWM 输出 (PE9 → AD9226 CLKIN, 2.048 MHz)
     * 注:CubeMX 配的 TIM1_CH1 是 PE9(非默认 PA8),所以模块 CLK 实际接到 PE9 */
    if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK) {
        Error_Handler();
    }
}

void AD9226_Start(void)
{
    s_adc_done = false;
    s_frame_finalized = false;

    /* 等效采样模式下捕获本帧实际使用的 divider(TIM1 ARR+1),
     * 避免命令中途到达导致重建使用错误的 divider。 */
    if (s_eq_mode) {
        s_eq_divider_cap = (uint16_t)(__HAL_TIM_GET_AUTORELOAD(&htim1) + 1U);
    } else {
        s_eq_divider_cap = 0U;
    }

    /* DMA 每次读 GPIOF->IDR 低16位 → 内存 (16 bit), 触发源 = TIM1_UP */
    if (HAL_DMA_Start_IT(&hdma_tim1_up,
                         (uint32_t)(uintptr_t)&GPIOF->IDR,
                         (uint32_t)(uintptr_t)s_adc_buf,
                         AD9226_SAMPLE_COUNT) != HAL_OK) {
        Error_Handler();
    }
    /* 使能 TIM1 的更新事件触发 DMA 请求 */
    __HAL_TIM_ENABLE_DMA(&htim1, TIM_DMA_UPDATE);
}

bool AD9226_IsDone(void)
{
    return s_adc_done;
}

bool AD9226_FinalizeFrame(void)
{
    if (!s_adc_done)
    {
        return false;
    }
    if (!s_frame_finalized)
    {
        for (uint32_t i = 0U; i < AD9226_SAMPLE_COUNT; ++i)
        {
            s_adc_buf[i] &= 0x0fffU;
        }
        s_frame_finalized = true;
    }
    return true;
}

const uint16_t *AD9226_GetBuf(void)
{
    return s_adc_buf;
}

/**
 * @brief  DMA 传输完成回调 (HAL 库会调用)
 * @note   中断里只置 flag + 关 DMA 请求,不做任何处理
 */
void HAL_DMA_XferCpltCallback(DMA_HandleTypeDef *hdma)
{
    if (hdma == &hdma_tim1_up) {
        __HAL_TIM_DISABLE_DMA(&htim1, TIM_DMA_UPDATE);
        s_adc_done = true;
    }
}

/* TIM1_CLK = 168 MHz, PSC = 0 → Fs = 168e6 / (ARR+1)
 * G 题可用范围:5 kHz (ARR=33599) ~ 20 MHz (ARR=7)
 * 上限 20 MHz 留余量给 DMA 读 GPIOF->IDR */
#define AD9226_FS_MIN  5000U
#define AD9226_FS_MAX  20000000U

HAL_StatusTypeDef AD9226_SetSampleRate(uint32_t fs_hz)
{
    if (fs_hz < AD9226_FS_MIN || fs_hz > AD9226_FS_MAX) {
        return HAL_ERROR;
    }
    /* ARR = 168_000_000 / fs_hz - 1
     * 例:fs=2048780 → ARR = 168M/2048780 - 1 = 82-1 = 81 ✅ */
    uint32_t arr = 168000000UL / fs_hz;
    if (arr > 0) arr -= 1;
    if (arr > 0xFFFFU) arr = 0xFFFFU;
    /* 写 ARR(下次 update event 生效,因为 AutoReloadPreload=ENABLE) */
    __HAL_TIM_SET_AUTORELOAD(&htim1, (uint16_t)arr);
    /* 同步 PWM 占空比为 50%:Pulse = (ARR+1)/2 - 1 */
    uint32_t pulse = (arr + 1U) / 2U;
    if (pulse > 0) pulse -= 1U;
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, (uint16_t)pulse);
    s_fs_hz = fs_hz;
    return HAL_OK;
}

uint32_t AD9226_GetSampleRate(void)
{
    return s_fs_hz;
}

/* ===== 等效采样模式实现 ===== */

HAL_StatusTypeDef AD9226_SetEqFundamental(uint32_t f0_hz)
{
    if (f0_hz == 0UL) {
        return HAL_ERROR;
    }

    /* 选一个最佳分频值,使 4096 点覆盖尽量多的相位格 */
    uint16_t divider = EsChooseTimerDivider(f0_hz);
    if (divider < ES_TIMER_DIVIDER_MIN || divider > ES_TIMER_DIVIDER_MAX) {
        return HAL_ERROR;
    }

    /* TIM1 实际 ARR = divider - 1,Fs = 168MHz / divider */
    uint32_t arr = (uint32_t)divider - 1U;
    /* 占空比尽量接近 50%:Pulse+1 = round(divider/2) */
    uint32_t pulse = ((uint32_t)divider + 1U) / 2U - 1U;

    __HAL_TIM_SET_AUTORELOAD(&htim1, (uint16_t)arr);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, (uint16_t)pulse);

    s_eq_f0_hz = f0_hz;
    s_eq_divider_req = divider;
    s_eq_divider_cap = divider;   /* 初始值,Start 时会再次捕获 */
    s_fs_hz = ES_TIMER_CLOCK_HZ / divider;
    s_eq_mode = true;

    return HAL_OK;
}

void AD9226_ClearEqMode(void)
{
    s_eq_mode = false;
}

bool AD9226_IsEqMode(void)
{
    return s_eq_mode;
}

uint32_t AD9226_GetEqFundamental(void)
{
    return s_eq_f0_hz;
}

uint16_t AD9226_GetEqDivider(void)
{
    /* 返回帧启动时捕获的实际 divider,保证重建与采样时钟一致 */
    return s_eq_divider_cap;
}
