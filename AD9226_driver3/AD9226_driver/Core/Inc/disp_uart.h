/**
 * @file    disp_uart.h
 * @brief   F4_acq → F4_display 链路:USART2 DMA 发送 ScreenDataFrame (EDC2026 协议)
 *
 * 帧格式(1084 字节,小端,与 EDC2026_Screen 一致):
 *   见 screen_protocol.h
 *
 * 协议字段:
 *   - wave_count = 256 (一个基频周期 256 点)
 *   - fft_count  = 0   (F4_acq 不算 FFT)
 *   - mode       = 0   (默认显示波形,屏上 T_num 选 1/3 周期)
 *   - frequency_mhz : 基频(Hz) * 1000
 *   - vpp_uv / vrms_uv: 从重建的 signed 波形计算
 */
#ifndef DISP_UART_H
#define DISP_UART_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "screen_protocol.h"

/** AD9226 单端 3.3V 输入,12bit:1 LSB = 3.3/4096 V = ~806 uV */
#define DISP_VREF_UV_PER_LSB  806U
#define DISP_WAVE_POINTS      (SCREEN_WAVE_MAX_POINTS)   /* 256 */

/**
 * @brief  初始化
 */
void DispUart_Init(void);

/**
 * @brief  阻塞/非阻塞发一帧 1084 字节 ScreenDataFrame
 * @param  samples      signed int16 波形(范围 -2048..2047,1 周期 256 点)
 * @param  seq          批次号
 * @param  f0_hz        基频 Hz
 * @return HAL_OK / HAL_ERROR / HAL_BUSY
 * @note   内部自动算 vpp_uv / vrms_uv / harmonic[*] = 0,fft[] = 0
 */
HAL_StatusTypeDef DispUart_SendWaveform(const int16_t *samples,
                                        uint16_t count,
                                        uint32_t seq,
                                        uint32_t f0_hz);

/** @brief  累计成功发送帧数 */
uint32_t DispUart_GetTxCount(void);
/** @brief  发送错误计数 */
uint32_t DispUart_GetErrCount(void);
/** @brief  busy 跳过次数(上一次还没发完) */
uint32_t DispUart_GetSkippedCount(void);

#endif /* DISP_UART_H */
