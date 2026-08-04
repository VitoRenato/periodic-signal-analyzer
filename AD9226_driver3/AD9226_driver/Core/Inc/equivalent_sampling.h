#ifndef EQUIVALENT_SAMPLING_H
#define EQUIVALENT_SAMPLING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 模块固定接收4096点，并把一个基频周期重建为256点。
 * 适配到当前 F407 工程:TIM1 在 APB2 上跑 168 MHz,给 AD9226 提供 CLK。
 * divider 28~64 等价于原 84 MHz 工程的 14~32,采样率范围不变(2.625~6 MHz)。 */
#define ES_TIMER_CLOCK_HZ       168000000UL
#define ES_TIMER_DIVIDER_MIN    28U
#define ES_TIMER_DIVIDER_MAX    64U
#define ES_EQ_SAMPLE_COUNT      4096U
#define ES_PERIOD_POINTS        256U
#define ES_MAX_DISPLAY_POINTS   768U
#define ES_PI                   3.14159265358979323846f

/* 最近一次发布波形的批次和采样参数。 */
typedef struct {
    uint32_t sequence;          /* 采样批次序号 */
    uint32_t fundamental_hz;    /* 重建使用的整数Hz基频 */
    uint32_t sample_rate_hz;    /* 本批实际采样率 */
    uint16_t point_count;       /* 复制接口实际返回的点数 */
    uint8_t valid;              /* 已发布有效波形时为1 */
} EsWaveformInfo;

/* 根据基频选择下一批TIM2分频值，返回范围为14～32。 */
uint16_t EsChooseTimerDivider(uint32_t fundamental_hz);

/*
 * 把本批4096点按基频相位折叠成一周期256点。
 * timer_divider必须是本批采样时实际使用的分频值。
 */
bool EsReconstructPeriod(
    const int16_t samples[ES_EQ_SAMPLE_COUNT],
    uint32_t fundamental_hz,
    uint16_t timer_divider,
    int16_t period[ES_PERIOD_POINTS]);

/* 普通采样模式：从 raw ADC 中用基频直接截取/重采样一个周期。
 * 不要求 timer_divider 在等效采样范围内，只要 raw buffer 里包含至少一个
 * 完整周期（fs_hz / freq_hz <= sample_count）即可得到静止波形。
 */
bool NormalReconstructPeriod(
    const int16_t samples[ES_EQ_SAMPLE_COUNT],
    uint32_t sample_count,
    uint32_t freq_hz,
    uint32_t fs_hz,
    int16_t period[ES_PERIOD_POINTS]);

/* 对 256 点一个周期做 FFT。
 * 同时输出两组结果：
 *   raw_spectrum     : 原始 FFT 模值（uint32，用于谐波比例/阈值计算）
 *   display_spectrum : 缩放到 ADC LSB 幅度的幅度谱（uint16，用于波形显示）
 */
bool ComputePeriodSpectrum(
    const int16_t period[ES_PERIOD_POINTS],
    uint32_t raw_spectrum[ES_PERIOD_POINTS],
    uint16_t display_spectrum[ES_PERIOD_POINTS]);
void EsWaveform_Publish(
    const int16_t period[ES_PERIOD_POINTS],
    uint32_t sequence,
    uint32_t fundamental_hz,
    uint16_t timer_divider);

/*
 * 复制1周期256点或3周期768点到调用者缓冲区。
 * 返回0表示尚无有效波形、周期数错误或缓冲区容量不足。
 */
uint16_t EsWaveform_Copy(
    int16_t *destination,
    uint16_t capacity,
    uint16_t cycles,
    EsWaveformInfo *info);

#ifdef __cplusplus
}
#endif

#endif
