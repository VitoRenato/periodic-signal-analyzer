/**
 * @file    ad9226.h
 * @brief   AD9226 并行 ADC 采集驱动 (F407, TIM1 PWM + DMA 触发)
 * @note    12 bit 数据落在 GPIOF->IDR 低 12 位
 */
#ifndef AD9226_H
#define AD9226_H

#include <stdbool.h>
#include <stdint.h>
#include "main.h"   /* HAL_StatusTypeDef */

/* 一帧采样数 (G 题 N=4096) */
#define AD9226_SAMPLE_COUNT  (4096u)

/**
 * @brief  初始化:启动 TIM1 PWM (PE9 出 2.048 MHz) + 配好 DMA 通道
 * @note   调用前需先 MX_TIM1_Init / MX_GPIO_Init / MX_DMA_Init
 *         注:CubeMX 把 TIM1_CH1 配在 PE9(不是默认 PA8),按板排针实际位置接
 */
void AD9226_Init(void);

/**
 * @brief  启动一次 4096 点采集
 * @note   必须先 Init。中途完成前不要再次 Start
 */
void AD9226_Start(void);

/** @brief  查询本次采集是否完成 (非阻塞) */
bool AD9226_IsDone(void);

/**
 * @brief  保留DMA帧低12位原始ADC码，仅清除GPIO的bit12~bit15
 * @note   同一帧重复调用不会再次改写数据;必须在GetBuf和SPI发送之前调用
 */
bool AD9226_FinalizeFrame(void);

/** @brief  取原始采样缓冲区；DMA完成并Finalize后有效码范围为0~4095 */
const uint16_t *AD9226_GetBuf(void);

/**
 * @brief  动态设置 AD9226 采样率 (Hz),实际是改 TIM1 ARR
 * @param  fs_hz : 目标采样率,合法范围 5 000 ~ 20 000 000
 * @return HAL_OK = 设置成功, HAL_ERROR = 参数非法
 * @note   在 AD9226_Start() 之前或之后调用都行,下一次 update event 生效
 *         (要求 TIM1 AutoReloadPreload = ENABLE)
 */
HAL_StatusTypeDef AD9226_SetSampleRate(uint32_t fs_hz);

/** @brief  取当前采样率(Hz) */
uint32_t AD9226_GetSampleRate(void);

/* ===== 等效采样模式(Equivalent Sampling)接口 ===== */

/**
 * @brief  进入等效采样模式,按基频 f0 选择最佳 TIM1 分频
 * @param  f0_hz : 信号基频(Hz),必须 > 0
 * @return HAL_OK = 成功, HAL_ERROR = 参数非法
 * @note   内部会调用 EsChooseTimerDivider 选一个 28~64 的分频值,
 *         并把 TIM1 配置到 Fs = 168MHz / divider。
 *         同时记录 f0 和 divider,供后续 EsReconstructPeriod 使用。
 */
HAL_StatusTypeDef AD9226_SetEqFundamental(uint32_t f0_hz);

/** @brief  退出等效采样模式,恢复普通同步采样(不改变当前 Fs) */
void AD9226_ClearEqMode(void);

/** @brief  当前是否处于等效采样模式 */
bool AD9226_IsEqMode(void);

/** @brief  当前等效采样使用的基频 f0(Hz) */
uint32_t AD9226_GetEqFundamental(void);

/** @brief  当前等效采样使用的 TIM1 分频值(28~64) */
uint16_t AD9226_GetEqDivider(void);

#endif /* AD9226_H */
