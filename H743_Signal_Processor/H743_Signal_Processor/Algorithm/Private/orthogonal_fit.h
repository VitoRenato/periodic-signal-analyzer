#ifndef ORTHOGONAL_FIT_H
#define ORTHOGONAL_FIT_H

#include "harmonic_model.h"
#include "signal_analyzer.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    double dc_V;
    SignalComponent tone[SIGNAL_MAX_FIT_TONES];
    double residual_rms_V;
    double snr_dB;
    double pivot_ratio;
    uint32_t fitted_sample_count;
    uint32_t clip_violation_count;
    double clip_violation_rms_V;
} OrthogonalFitResult;

bool OrthogonalFit_Run(
    const float samples[SIGNAL_FRAME_SAMPLES],
    uint32_t sample_rate_Hz,
    const HarmonicModel *model,
    OrthogonalFitResult *fit);

/**
 * @brief 只使用 valid_mask 非零的采样拟合，并检查被屏蔽点的下削顶约束。
 *
 * valid_mask 为 NULL 时等价于所有样本有效；被屏蔽样本不进入法方程，
 * 其模型预测值高于 lower_clip_threshold_V 时计为约束违反。
 */
bool OrthogonalFit_RunMasked(
    const float samples[SIGNAL_FRAME_SAMPLES],
    const uint8_t valid_mask[SIGNAL_FRAME_SAMPLES],
    double lower_clip_threshold_V,
    uint32_t sample_rate_Hz,
    const HarmonicModel *model,
    OrthogonalFitResult *fit);

#endif
