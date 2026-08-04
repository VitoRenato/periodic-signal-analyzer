#ifndef SIGNAL_PREPROCESS_H
#define SIGNAL_PREPROCESS_H

#include "signal_analyzer.h"

#include <stdint.h>

#define SIGNAL_LOWER_CLIP_CODE_MIN    2048U
#define SIGNAL_LOWER_CLIP_CODE_MAX    2050U
#define SIGNAL_LOWER_VALID_CODE_MIN   2053U
#define SIGNAL_LOWER_CLIP_MIN_COUNT     41U
#define SIGNAL_LOWER_CLIP_MIN_RUN         8U

/**
 * @brief AD9226 一帧预处理的内部统计量。
 *
 * 此类型只在算法实现和主机数值测试中使用，不属于板级或显示协议接口。
 */
typedef struct
{
    uint32_t rail_code_count;
    uint32_t high_bit_count;
    uint32_t lower_clip_count;
    uint32_t below_lower_clip_count;
    uint32_t valid_sample_count;
    uint32_t longest_clip_run;
    uint8_t lower_clip_detected;
    double dc_mean_V;
    double ac_energy_V2;
    double lower_clip_threshold_V;
} SignalPreprocessStats;

/**
 * @brief 掩码、标定并检测一帧采样中的下限平台。
 *
 * fit_samples 保留未去直流的 BNC 电压，供联合拟合估计 DC；
 * spectrum_samples 去除整帧均值，供 FFT/CZT 使用；valid_mask 在检测到
 * 下限平台时屏蔽平台及安全余量内的样本，否则保持全 1。
 */
void SignalPreprocess_Run(
    const uint16_t samples[SIGNAL_FRAME_SAMPLES],
    float fit_samples[SIGNAL_FRAME_SAMPLES],
    float spectrum_samples[SIGNAL_FRAME_SAMPLES],
    uint8_t valid_mask[SIGNAL_FRAME_SAMPLES],
    SignalPreprocessStats *stats);

#endif
