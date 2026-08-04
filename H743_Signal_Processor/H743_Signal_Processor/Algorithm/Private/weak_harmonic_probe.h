#ifndef WEAK_HARMONIC_PROBE_H
#define WEAK_HARMONIC_PROBE_H

#include "harmonic_model.h"
#include "orthogonal_fit.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 在可靠基频模型上定向搜索弱整数次谐波。
 *
 * 未找到可信候选也属于成功；仅参数非法或基准模型无法重建时返回 false。
 * 调用方应在 false 时保留传入前的 model 和 fit。
 */
bool WeakHarmonicProbe_Augment(
    const float samples[SIGNAL_FRAME_SAMPLES],
    uint32_t sample_rate_Hz,
    HarmonicModel *model,
    OrthogonalFitResult *fit);

#endif
