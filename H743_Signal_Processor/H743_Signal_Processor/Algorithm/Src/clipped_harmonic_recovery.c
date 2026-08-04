#include "clipped_harmonic_recovery.h"

#include <math.h>
#include <stddef.h>

#define CLIPPED_RECOVERY_MIN_VALID_SAMPLES \
    (SIGNAL_FRAME_SAMPLES / 4U)
#define CLIPPED_RECOVERY_MIN_TONE_AMPLITUDE_V 0.002
#define CLIPPED_RECOVERY_MAX_RESIDUAL_RMS_V 0.005
#define CLIPPED_RECOVERY_MAX_VIOLATION_RMS_V 0.005
#define CLIPPED_RECOVERY_MAX_VIOLATION_PERCENT 15U
#define CLIPPED_RECOVERY_MIN_PIVOT_RATIO 1.0e-10

static int ClippedHarmonicRecovery_FitIsAcceptable(
    const SignalPreprocessStats *preprocess,
    const OrthogonalFitResult *fit)
{
    return (fit->fitted_sample_count >= CLIPPED_RECOVERY_MIN_VALID_SAMPLES)
        && isfinite(fit->residual_rms_V)
        && (fit->residual_rms_V <= CLIPPED_RECOVERY_MAX_RESIDUAL_RMS_V)
        && isfinite(fit->clip_violation_rms_V)
        && (fit->clip_violation_rms_V
            <= CLIPPED_RECOVERY_MAX_VIOLATION_RMS_V)
        && (fit->pivot_ratio >= CLIPPED_RECOVERY_MIN_PIVOT_RATIO)
        && ((preprocess->lower_clip_count == 0U)
            || ((fit->clip_violation_count * 100U)
                <= (preprocess->lower_clip_count
                    * CLIPPED_RECOVERY_MAX_VIOLATION_PERCENT)));
}

static int ClippedHarmonicRecovery_PruneModel(
    const HarmonicModel *candidate,
    const OrthogonalFitResult *candidate_fit,
    HarmonicModel *pruned)
{
    size_t tone_index;
    int have_desired = 0;

    *pruned = (HarmonicModel){0};
    for (tone_index = 0U;
         tone_index < (size_t)candidate->tone_count;
         ++tone_index)
    {
        const int keep_interference = candidate->desired[tone_index] == 0U;
        const int keep_desired = (candidate->desired[tone_index] != 0U)
            && isfinite(candidate_fit->tone[tone_index].amplitude_peak_V)
            && ((double)candidate_fit->tone[tone_index].amplitude_peak_V
                >= CLIPPED_RECOVERY_MIN_TONE_AMPLITUDE_V);

        if (!keep_interference && !keep_desired)
        {
            continue;
        }

        pruned->frequency_Hz[pruned->tone_count] =
            candidate->frequency_Hz[tone_index];
        pruned->desired[pruned->tone_count] =
            candidate->desired[tone_index];
        if (keep_desired)
        {
            have_desired = 1;
        }
        ++pruned->tone_count;
    }

    return have_desired;
}

bool ClippedHarmonicRecovery_Select(
    const float samples[SIGNAL_FRAME_SAMPLES],
    const uint8_t valid_mask[SIGNAL_FRAME_SAMPLES],
    const SignalPreprocessStats *preprocess,
    uint32_t sample_rate_Hz,
    const CztRefinedPeak peaks[SIGNAL_MAX_CANDIDATES],
    size_t peak_count,
    HarmonicModel *model,
    OrthogonalFitResult *fit)
{
    HarmonicModel candidate;
    HarmonicModel pruned;
    OrthogonalFitResult candidate_fit;

    if ((samples == NULL)
        || (valid_mask == NULL)
        || (preprocess == NULL)
        || (peaks == NULL)
        || (model == NULL)
        || (fit == NULL)
        || (preprocess->lower_clip_detected == 0U)
        || (preprocess->valid_sample_count
            < CLIPPED_RECOVERY_MIN_VALID_SAMPLES)
        || !HarmonicModel_Select(peaks, peak_count, &candidate)
        || !OrthogonalFit_RunMasked(
            samples,
            valid_mask,
            preprocess->lower_clip_threshold_V,
            sample_rate_Hz,
            &candidate,
            &candidate_fit)
        || !ClippedHarmonicRecovery_PruneModel(
            &candidate,
            &candidate_fit,
            &pruned)
        || !OrthogonalFit_RunMasked(
            samples,
            valid_mask,
            preprocess->lower_clip_threshold_V,
            sample_rate_Hz,
            &pruned,
            fit)
        || !ClippedHarmonicRecovery_FitIsAcceptable(preprocess, fit))
    {
        if (model != NULL)
        {
            *model = (HarmonicModel){0};
        }
        if (fit != NULL)
        {
            *fit = (OrthogonalFitResult){0};
        }
        return false;
    }

    *model = pruned;
    return true;
}
