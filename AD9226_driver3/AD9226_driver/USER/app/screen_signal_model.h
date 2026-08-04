#ifndef SCREEN_SIGNAL_MODEL_H
#define SCREEN_SIGNAL_MODEL_H

#include "h743_measurement_model.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCREEN_SIGNAL_POINTS_PER_PERIOD 256U
#define SCREEN_SIGNAL_MAX_LINES 3U

typedef struct
{
    uint32_t frequency_mHz;
    uint32_t amplitude_peak_uV;
} ScreenSignalSpectrumLine;

typedef struct
{
    uint8_t count;
    ScreenSignalSpectrumLine line[SCREEN_SIGNAL_MAX_LINES];
} ScreenSignalSpectrum;

/**
 * @brief 按 H743 的余弦相位模型生成 1 或 3 个基波周期，输出单位 uV。
 */
bool ScreenSignalModel_GenerateWave(
    const H743MeasurementModel *model,
    uint8_t periods,
    int32_t *wave_uV,
    size_t capacity);

/**
 * @brief 生成以 2048 表示 0 V 的 12 位显示波形码。
 */
bool ScreenSignalModel_GenerateWaveCodes(
    const H743MeasurementModel *model,
    uint8_t periods,
    uint16_t *wave_codes,
    size_t capacity);

/** @brief 输出各期望分量的真实频率和峰值幅度。 */
bool ScreenSignalModel_FillSpectrum(
    const H743MeasurementModel *model,
    ScreenSignalSpectrum *spectrum);

/** @brief 把每个离散频率分量映射为唯一一个频谱 bin。 */
bool ScreenSignalModel_RasterizeSpectrum(
    const ScreenSignalSpectrum *spectrum,
    uint32_t maximum_frequency_mHz,
    uint16_t *bins,
    size_t bin_count);

#endif
