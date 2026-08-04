#include "signal_preprocess.h"
#include "ad9226_calibration.h"

#include <stddef.h>

#define AD9226_CODE_MASK 0x0FFFU

void SignalPreprocess_Run(
    const uint16_t samples[SIGNAL_FRAME_SAMPLES],
    float fit_samples[SIGNAL_FRAME_SAMPLES],
    float spectrum_samples[SIGNAL_FRAME_SAMPLES],
    uint8_t valid_mask[SIGNAL_FRAME_SAMPLES],
    SignalPreprocessStats *stats)
{
    double sum = 0.0;
    uint32_t current_clip_run = 0U;
    size_t index;

    stats->rail_code_count = 0U;
    stats->high_bit_count = 0U;
    stats->lower_clip_count = 0U;
    stats->below_lower_clip_count = 0U;
    stats->valid_sample_count = 0U;
    stats->longest_clip_run = 0U;
    stats->lower_clip_detected = 0U;
    stats->dc_mean_V = 0.0;
    stats->ac_energy_V2 = 0.0;
    stats->lower_clip_threshold_V =
        (double)Ad9226Calibration_CodeToBncV(
            (uint16_t)SIGNAL_LOWER_CLIP_CODE_MAX);

    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        const uint16_t code = samples[index] & AD9226_CODE_MASK;
        if ((samples[index] & (uint16_t)(~AD9226_CODE_MASK)) != 0U)
        {
            ++stats->high_bit_count;
        }

        if ((code == 0U) || (code == AD9226_CODE_MASK))
        {
            ++stats->rail_code_count;
        }

        if ((code >= SIGNAL_LOWER_CLIP_CODE_MIN)
            && (code <= SIGNAL_LOWER_CLIP_CODE_MAX))
        {
            ++stats->lower_clip_count;
            ++current_clip_run;
        }
        else
        {
            if (current_clip_run > stats->longest_clip_run)
            {
                stats->longest_clip_run = current_clip_run;
            }
            current_clip_run = 0U;
        }

        if (code < SIGNAL_LOWER_CLIP_CODE_MIN)
        {
            ++stats->below_lower_clip_count;
        }

        fit_samples[index] = Ad9226Calibration_CodeToBncV(code);
        sum += (double)fit_samples[index];
    }

    if (current_clip_run > stats->longest_clip_run)
    {
        stats->longest_clip_run = current_clip_run;
    }

    stats->lower_clip_detected =
        ((stats->below_lower_clip_count == 0U)
            && ((stats->lower_clip_count >= SIGNAL_LOWER_CLIP_MIN_COUNT)
                || (stats->longest_clip_run >= SIGNAL_LOWER_CLIP_MIN_RUN)))
        ? 1U
        : 0U;
    stats->dc_mean_V = sum / (double)SIGNAL_FRAME_SAMPLES;
    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        const double centered_sample =
            (double)fit_samples[index] - stats->dc_mean_V;
        const uint16_t code = samples[index] & AD9226_CODE_MASK;

        spectrum_samples[index] = (float)centered_sample;
        stats->ac_energy_V2 += centered_sample * centered_sample;

        if ((stats->lower_clip_detected == 0U)
            || (code >= SIGNAL_LOWER_VALID_CODE_MIN))
        {
            valid_mask[index] = 1U;
            ++stats->valid_sample_count;
        }
        else
        {
            valid_mask[index] = 0U;
        }
    }
}
