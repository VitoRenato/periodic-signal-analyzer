#include "screen_signal_model.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#define SCREEN_SIGNAL_PI 3.14159265358979323846
#define SCREEN_SIGNAL_MAX_DESIRED_FREQUENCY_HZ 500000U

/* 768点工作区固定放静态RAM，避免F407仅1 KB保留栈被3 KB局部数组压坏。 */
static int32_t g_generated_wave_uV[
    3U * SCREEN_SIGNAL_POINTS_PER_PERIOD];

static int ScreenSignalModel_Validate(const H743MeasurementModel *model)
{
    size_t component_index;

    if ((model == NULL)
        || (model->fundamental_Hz == 0U)
        || (model->component_count == 0U)
        || (model->component_count > H743_MEASUREMENT_MAX_COMPONENTS))
    {
        return 0;
    }

    for (component_index = 0U;
         component_index < (size_t)model->component_count;
         ++component_index)
    {
        const H743MeasurementComponent *component =
            &model->component[component_index];
        const uint64_t frequency_Hz =
            (uint64_t)model->fundamental_Hz * (uint64_t)component->order;
        size_t previous_index;

        if ((component->valid == 0U)
            || (component->order == 0U)
            || (frequency_Hz > SCREEN_SIGNAL_MAX_DESIRED_FREQUENCY_HZ)
            || ((component_index == 0U) && (component->order != 1U)))
        {
            return 0;
        }
        for (previous_index = 0U;
             previous_index < component_index;
             ++previous_index)
        {
            if (model->component[previous_index].order == component->order)
            {
                return 0;
            }
        }
    }
    return 1;
}

bool ScreenSignalModel_GenerateWave(
    const H743MeasurementModel *model,
    uint8_t periods,
    int32_t *wave_uV,
    size_t capacity)
{
    const size_t required = (size_t)periods
        * SCREEN_SIGNAL_POINTS_PER_PERIOD;
    size_t sample_index;

    if (!ScreenSignalModel_Validate(model)
        || ((periods != 1U) && (periods != 3U))
        || (wave_uV == NULL)
        || (capacity < required))
    {
        return false;
    }

    for (sample_index = 0U;
         sample_index < SCREEN_SIGNAL_POINTS_PER_PERIOD;
         ++sample_index)
    {
        const double fundamental_phase = 2.0 * SCREEN_SIGNAL_PI
            * (double)sample_index
            / (double)SCREEN_SIGNAL_POINTS_PER_PERIOD;
        double sample_uV = 0.0;
        size_t component_index;

        for (component_index = 0U;
             component_index < (size_t)model->component_count;
             ++component_index)
        {
            const H743MeasurementComponent *component =
                &model->component[component_index];
            const double phase_rad = (double)component->phase_q15
                * SCREEN_SIGNAL_PI / 32768.0;

            sample_uV += (double)component->amplitude_peak_uV
                * cos(((double)component->order * fundamental_phase)
                    + phase_rad);
        }

        if (sample_uV >= (double)INT32_MAX)
        {
            g_generated_wave_uV[sample_index] = INT32_MAX;
        }
        else if (sample_uV <= (double)INT32_MIN)
        {
            g_generated_wave_uV[sample_index] = INT32_MIN;
        }
        else
        {
            g_generated_wave_uV[sample_index] = (int32_t)((sample_uV >= 0.0)
                ? floor(sample_uV + 0.5)
                : ceil(sample_uV - 0.5));
        }
    }

    for (sample_index = SCREEN_SIGNAL_POINTS_PER_PERIOD;
         sample_index < required;
         ++sample_index)
    {
        g_generated_wave_uV[sample_index] =
            g_generated_wave_uV[
                sample_index % SCREEN_SIGNAL_POINTS_PER_PERIOD];
    }
    for (sample_index = 0U; sample_index < required; ++sample_index)
    {
        wave_uV[sample_index] = g_generated_wave_uV[sample_index];
    }
    return true;
}

static bool ScreenSignalModel_EncodeWaveCodes(
    const int32_t *wave_uV,
    size_t count,
    uint16_t *wave_codes,
    size_t capacity)
{
    int64_t maximum_absolute_uV = 0;
    size_t index;

    if ((wave_uV == NULL) || (wave_codes == NULL)
        || (count == 0U) || (capacity < count))
    {
        return false;
    }

    for (index = 0U; index < count; ++index)
    {
        const int64_t value = (int64_t)wave_uV[index];
        const int64_t absolute = (value < 0) ? -value : value;

        if (absolute > maximum_absolute_uV)
        {
            maximum_absolute_uV = absolute;
        }
    }

    if (maximum_absolute_uV == 0)
    {
        for (index = 0U; index < count; ++index)
        {
            wave_codes[index] = 2048U;
        }
        return true;
    }

    for (index = 0U; index < count; ++index)
    {
        const int64_t scaled = ((int64_t)wave_uV[index] * 2047)
            / maximum_absolute_uV;
        int64_t code = 2048 + scaled;

        if (code < 1)
        {
            code = 1;
        }
        if (code > 4095)
        {
            code = 4095;
        }
        wave_codes[index] = (uint16_t)code;
    }
    return true;
}

bool ScreenSignalModel_GenerateWaveCodes(
    const H743MeasurementModel *model,
    uint8_t periods,
    uint16_t *wave_codes,
    size_t capacity)
{
    const size_t required = (size_t)periods
        * SCREEN_SIGNAL_POINTS_PER_PERIOD;

    if ((wave_codes == NULL) || (capacity < required)
        || !ScreenSignalModel_GenerateWave(
            model,
            periods,
            g_generated_wave_uV,
            3U * SCREEN_SIGNAL_POINTS_PER_PERIOD))
    {
        return false;
    }
    return ScreenSignalModel_EncodeWaveCodes(
        g_generated_wave_uV,
        required,
        wave_codes,
        capacity);
}

bool ScreenSignalModel_FillSpectrum(
    const H743MeasurementModel *model,
    ScreenSignalSpectrum *spectrum)
{
    ScreenSignalSpectrum generated = {0};
    size_t component_index;

    if (!ScreenSignalModel_Validate(model) || (spectrum == NULL))
    {
        return false;
    }

    generated.count = model->component_count;
    for (component_index = 0U;
         component_index < (size_t)model->component_count;
         ++component_index)
    {
        generated.line[component_index].frequency_mHz =
            model->fundamental_Hz
            * (uint32_t)model->component[component_index].order
            * 1000U;
        generated.line[component_index].amplitude_peak_uV =
            model->component[component_index].amplitude_peak_uV;
    }
    *spectrum = generated;
    return true;
}

bool ScreenSignalModel_RasterizeSpectrum(
    const ScreenSignalSpectrum *spectrum,
    uint32_t maximum_frequency_mHz,
    uint16_t *bins,
    size_t bin_count)
{
    size_t line_index;

    if ((spectrum == NULL)
        || (bins == NULL)
        || (bin_count < 3U)
        || (maximum_frequency_mHz == 0U)
        || (spectrum->count == 0U)
        || (spectrum->count > SCREEN_SIGNAL_MAX_LINES))
    {
        return false;
    }

    (void)memset(bins, 0, bin_count * sizeof(bins[0U]));
    for (line_index = 0U;
         line_index < (size_t)spectrum->count;
         ++line_index)
    {
        const ScreenSignalSpectrumLine *line = &spectrum->line[line_index];
        const uint64_t rounded_bin =
            ((uint64_t)line->frequency_mHz * (uint64_t)(bin_count - 1U)
                + ((uint64_t)maximum_frequency_mHz / 2U))
            / (uint64_t)maximum_frequency_mHz;
        uint32_t amplitude_mV = (line->amplitude_peak_uV + 500U) / 1000U;
        size_t bin;

        if ((line->frequency_mHz > maximum_frequency_mHz)
            || (line->amplitude_peak_uV == 0U)
            || (rounded_bin >= bin_count))
        {
            return false;
        }
        if (amplitude_mV == 0U) amplitude_mV = 1U;
        if (amplitude_mV > UINT16_MAX) amplitude_mV = UINT16_MAX;
        bin = (size_t)rounded_bin;

        if (bins[bin] < (uint16_t)amplitude_mV)
        {
            bins[bin] = (uint16_t)amplitude_mV;
        }
    }
    return true;
}
