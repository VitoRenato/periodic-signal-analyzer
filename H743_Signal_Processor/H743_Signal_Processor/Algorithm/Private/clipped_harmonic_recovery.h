#ifndef CLIPPED_HARMONIC_RECOVERY_H
#define CLIPPED_HARMONIC_RECOVERY_H

#include "czt_zoom.h"
#include "harmonic_model.h"
#include "orthogonal_fit.h"
#include "signal_preprocess.h"
#include "spectrum_detect.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 从下限削顶帧中筛除伪谐波，并用有效样本恢复周期模型。
 *
 * 频谱峰只用于提出候选；最终保留哪些谱线由未削顶样本的联合拟合幅度
 * 决定。函数返回前会重新拟合剪枝后的模型，并检查削顶平台不等式。
 */
bool ClippedHarmonicRecovery_Select(
    const float samples[SIGNAL_FRAME_SAMPLES],
    const uint8_t valid_mask[SIGNAL_FRAME_SAMPLES],
    const SignalPreprocessStats *preprocess,
    uint32_t sample_rate_Hz,
    const CztRefinedPeak peaks[SIGNAL_MAX_CANDIDATES],
    size_t peak_count,
    HarmonicModel *model,
    OrthogonalFitResult *fit);

#endif
