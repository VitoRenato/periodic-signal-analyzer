#include "disp_uart.h"
#include "main.h"
#include <string.h>
#include <math.h>

extern UART_HandleTypeDef huart2;

/* 静态发送缓冲:固定 1084 字节,放 bss(由链接器自动对齐) */
static ScreenDataFrame s_frame;
static volatile bool     s_tx_busy = false;
static volatile uint32_t s_tx_ok    = 0;
static volatile uint32_t s_tx_err   = 0;
static volatile uint32_t s_tx_skip  = 0;

static DMA_HandleTypeDef hdma_tx;

/* sum32 校验 */
static uint32_t sum32(const uint8_t *p, uint32_t n)
{
    uint32_t s = 0u;
    for (uint32_t i = 0; i < n; i++) s += p[i];
    return s;
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2) {
        s_tx_busy = false;
        s_tx_ok++;
    }
}

static void mx_dma_init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();
    hdma_tx.Instance                 = DMA1_Stream6;
    hdma_tx.Init.Channel             = DMA_CHANNEL_4;     /* USART2_TX */
    hdma_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_tx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_tx.Init.MemDataAlignment    = DMA_PDATAALIGN_BYTE;
    hdma_tx.Init.Mode                = DMA_NORMAL;
    hdma_tx.Init.Priority            = DMA_PRIORITY_MEDIUM;
    hdma_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_tx) != HAL_OK) Error_Handler();
    __HAL_LINKDMA(&huart2, hdmatx, hdma_tx);
    HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);
}

/* 中断入口(stm32f4xx_it.c 里可能没有 DMA1_Stream6,自己声明) */
void DMA1_Stream6_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_tx);
}

void DispUart_Init(void)
{
    s_tx_busy = false;
    s_tx_ok   = 0;
    s_tx_err  = 0;
    s_tx_skip = 0;
    memset(&s_frame, 0, sizeof(s_frame));
    mx_dma_init();
}

HAL_StatusTypeDef DispUart_SendWaveform(const int16_t *samples,
                                        uint16_t count,
                                        uint32_t seq,
                                        uint32_t f0_hz)
{
    if ((samples == NULL) || (count == 0u) || (count > SCREEN_WAVE_MAX_POINTS)) {
        s_tx_err++;
        return HAL_ERROR;
    }
    if (s_tx_busy) {
        s_tx_skip++;
        return HAL_BUSY;
    }
    s_tx_busy = true;

    /* --- 计算 vpp / vrms --- */
    int32_t vmax = INT32_MIN, vmin = INT32_MAX;
    double  sq   = 0.0;
    for (uint16_t i = 0u; i < count; i++) {
        int32_t v = (int32_t)samples[i];
        if (v > vmax) vmax = v;
        if (v < vmin) vmin = v;
        sq += (double)v * (double)v;
    }
    int32_t vpp_lsb   = vmax - vmin;
    double  vrms_lsb  = sqrt(sq / (double)count);
    uint32_t vpp_uv   = (uint32_t)((uint64_t)vpp_lsb   * DISP_VREF_UV_PER_LSB);
    uint32_t vrms_uv  = (uint32_t)((uint64_t)vrms_lsb  * DISP_VREF_UV_PER_LSB);

    /* --- 组帧 --- */
    memset(&s_frame, 0, sizeof(s_frame));
    s_frame.magic           = SCREEN_FRAME_MAGIC;
    s_frame.version         = SCREEN_FRAME_VERSION;
    s_frame.byte_len        = (uint16_t)SCREEN_FRAME_SIZE;
    s_frame.sequence        = seq;
    s_frame.frequency_mhz   = f0_hz * 1000u;
    s_frame.vpp_uv          = vpp_uv;
    s_frame.vrms_uv         = vrms_uv;
    s_frame.wave_count      = count;
    s_frame.fft_count       = 0u;
    s_frame.mode            = SCREEN_MODE_WAVE;
    s_frame.wave_periods    = SCREEN_WAVE_PERIOD_3;  /* 默认 3 周期,屏上 T_num 可改 */
    /* harmonic 三个为 0(本端没算) */
    /* fft[] 全部 0 */

    /* samples: signed -2048..2047 → uint16 0..4095 */
    for (uint16_t i = 0u; i < count; i++) {
        int32_t v = (int32_t)samples[i] + 2048L;
        if (v < 0)     v = 0;
        if (v > 4095L) v = 4095L;
        s_frame.wave[i] = (uint16_t)v;
    }
    /* wave[count..255] 已是 0 */

    /* --- sum32 校验:前 1080 字节 --- */
    s_frame.checksum = sum32((const uint8_t *)&s_frame, SCREEN_FRAME_SIZE - 4u);

    /* --- 启动 DMA 发送 --- */
    HAL_StatusTypeDef st = HAL_UART_Transmit_DMA(&huart2,
                                                 (uint8_t *)&s_frame,
                                                 (uint16_t)SCREEN_FRAME_SIZE);
    if (st != HAL_OK) {
        s_tx_busy = false;
        s_tx_err++;
    }
    return st;
}

uint32_t DispUart_GetTxCount(void)      { return s_tx_ok;   }
uint32_t DispUart_GetErrCount(void)     { return s_tx_err;  }
uint32_t DispUart_GetSkippedCount(void) { return s_tx_skip; }
