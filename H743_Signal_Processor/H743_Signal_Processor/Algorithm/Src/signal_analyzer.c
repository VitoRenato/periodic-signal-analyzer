#include "signal_analyzer.h"
#include "clipped_harmonic_recovery.h"
#include "czt_zoom.h"
#include "harmonic_model.h"
#include "orthogonal_fit.h"
#include "signal_metrics.h"
#include "signal_preprocess.h"
#include "spectrum_detect.h"
#include "weak_harmonic_probe.h"

#include <stddef.h>

#define SIGNAL_MIN_SAMPLE_RATE_HZ 5000U
#define SIGNAL_MAX_SAMPLE_RATE_HZ 21000000U
#define SIGNAL_FULL_BAND_MIN_SAMPLE_RATE_HZ 1000000U
#define SIGNAL_DEFAULT_RECOMMENDED_SAMPLE_RATE_HZ 2048780U
#define SIGNAL_INTERFERENCE_RECOMMENDED_SAMPLE_RATE_HZ 10000000U
#define SIGNAL_DESIRED_MIN_FREQUENCY_HZ 10000.0f
#define SIGNAL_DESIRED_MAX_FREQUENCY_HZ 500000.0f
#define SIGNAL_EDGE_NORMALIZATION_HZ 500.0f
#define SIGNAL_INTERFERENCE_MIN_FREQUENCY_HZ 1000000.0f

/*
 * 4096点标定工作区固定放在静态存储区，避免占用MCU线程栈。
 * 当前公共处理函数以及各FFT/CZT静态工作区只允许单调用者串行使用。
 */
static float g_fit_samples[SIGNAL_FRAME_SAMPLES];
static float g_spectrum_samples[SIGNAL_FRAME_SAMPLES];
static uint8_t g_valid_mask[SIGNAL_FRAME_SAMPLES];
static OrthogonalFitCandidates g_model_candidates;

static void SignalAnalyzer_ResetResult(
    SignalResult *result,
    uint32_t sample_rate_Hz,
    SignalStatus status)
{
    *result = (SignalResult){0};
    result->status = status;
    result->sample_rate_Hz = sample_rate_Hz;
}

static void SignalAnalyzer_NormalizeRefinedFrequency(CztRefinedPeak *peak)
{
    if (peak->valid == 0U)
    {
        return;
    }

    if ((peak->frequency_Hz < SIGNAL_DESIRED_MIN_FREQUENCY_HZ)
        && (peak->frequency_Hz
            >= (SIGNAL_DESIRED_MIN_FREQUENCY_HZ
                - SIGNAL_EDGE_NORMALIZATION_HZ)))
    {
        peak->frequency_Hz = SIGNAL_DESIRED_MIN_FREQUENCY_HZ;
    }
    else if ((peak->frequency_Hz > SIGNAL_DESIRED_MAX_FREQUENCY_HZ)
        && (peak->frequency_Hz
            <= (SIGNAL_DESIRED_MAX_FREQUENCY_HZ
                + SIGNAL_EDGE_NORMALIZATION_HZ)))
    {
        peak->frequency_Hz = SIGNAL_DESIRED_MAX_FREQUENCY_HZ;
    }
}

static int SignalAnalyzer_HasInterference(const HarmonicModel *model)
{
    size_t tone_index;

    for (tone_index = 0U;
         tone_index < (size_t)model->tone_count;
         ++tone_index)
    {
        if ((model->desired[tone_index] == 0U)
            && (model->frequency_Hz[tone_index]
                >= SIGNAL_INTERFERENCE_MIN_FREQUENCY_HZ))
        {
            return 1;
        }
    }
    return 0;
}

static void SignalAnalyzer_ApplySampleRateStatus(
    uint32_t sample_rate_Hz,
    const HarmonicModel *model,
    SignalResult *result)
{
    result->status = SIGNAL_STATUS_VALID;
    result->recommended_sample_rate_Hz = 0U;

    if (sample_rate_Hz <= SIGNAL_FULL_BAND_MIN_SAMPLE_RATE_HZ)
    {
        result->status = SIGNAL_STATUS_SAMPLE_RATE_UNSAFE;
        result->recommended_sample_rate_Hz =
            SIGNAL_DEFAULT_RECOMMENDED_SAMPLE_RATE_HZ;
    }

    if (SignalAnalyzer_HasInterference(model)
        && (sample_rate_Hz
            < SIGNAL_INTERFERENCE_RECOMMENDED_SAMPLE_RATE_HZ))
    {
        result->status = SIGNAL_STATUS_SAMPLE_RATE_UNSAFE;
        result->recommended_sample_rate_Hz =
            SIGNAL_INTERFERENCE_RECOMMENDED_SAMPLE_RATE_HZ;
    }
}

static SignalStatus SignalAnalyzer_ProcessInternal(
    const uint16_t samples[SIGNAL_FRAME_SAMPLES],
    uint32_t sample_rate_Hz,
    SignalResult *result,
    bool calibrated)
{
    SignalPreprocessStats stats;
    SpectrumCandidate candidates[SIGNAL_MAX_CANDIDATES] = {{0}};
    CztRefinedPeak refined_peaks[SIGNAL_MAX_CANDIDATES] = {{0}};
    HarmonicModel model;
    OrthogonalFitResult fit;
    size_t candidate_count;
    size_t candidate_index;
    const float nyquist_Hz = 0.5f * (float)sample_rate_Hz;

    if (result == NULL)
    {
        return SIGNAL_STATUS_BAD_ARGUMENT;
    }

    SignalAnalyzer_ResetResult(
        result,
        sample_rate_Hz,
        SIGNAL_STATUS_BAD_ARGUMENT);

    if ((samples == NULL)
        || (sample_rate_Hz < SIGNAL_MIN_SAMPLE_RATE_HZ)
        || (sample_rate_Hz > SIGNAL_MAX_SAMPLE_RATE_HZ))
    {
        return result->status;
    }

    SignalPreprocess_Run(
        samples,
        g_fit_samples,
        g_spectrum_samples,
        g_valid_mask,
        &stats);

    if ((stats.high_bit_count != 0U)
        || ((stats.rail_code_count * 100U) > SIGNAL_FRAME_SAMPLES))
    {
        result->status = SIGNAL_STATUS_CLIPPED;
        return result->status;
    }

    if (stats.ac_energy_V2 <= 0.0)
    {
        result->status = SIGNAL_STATUS_NO_SIGNAL;
        return result->status;
    }

    candidate_count = SpectrumDetect_Find(
        g_spectrum_samples,
        sample_rate_Hz,
        0.0f,
        nyquist_Hz,
        candidates);
    if (candidate_count == 0U)
    {
        result->status = SIGNAL_STATUS_NO_SIGNAL;
        return result->status;
    }

    for (candidate_index = 0U;
         candidate_index < candidate_count;
         ++candidate_index)
    {
        refined_peaks[candidate_index] = CztZoom_Refine(
            g_spectrum_samples,
            sample_rate_Hz,
            candidates[candidate_index].coarse_frequency_Hz);
        SignalAnalyzer_NormalizeRefinedFrequency(
            &refined_peaks[candidate_index]);
    }

    if (stats.lower_clip_detected != 0U)
    {
        result->quality_flags |= SIGNAL_QUALITY_LOWER_CLIP_DETECTED;
        if (!ClippedHarmonicRecovery_Select(
                g_fit_samples,
                g_valid_mask,
                &stats,
                sample_rate_Hz,
                refined_peaks,
                candidate_count,
                &model,
                &fit))
        {
            result->status = SIGNAL_STATUS_CLIPPED;
            return result->status;
        }
        result->quality_flags |= SIGNAL_QUALITY_CLIPPED_MODEL_RECOVERED;
    }
    else
    {
        if (!OrthogonalFit_PrepareCandidates(
                g_fit_samples, sample_rate_Hz, refined_peaks,
                candidate_count, &g_model_candidates)
            || !HarmonicModel_SelectScored(
                refined_peaks, candidate_count,
                OrthogonalFit_ScoreModel, &g_model_candidates, &model))
        {
            result->status = SIGNAL_STATUS_MODEL_INVALID;
            return result->status;
        }
        if (!OrthogonalFit_Run(
                g_fit_samples,
                sample_rate_Hz,
                &model,
                &fit))
        {
            result->status = SIGNAL_STATUS_ILL_CONDITIONED;
            return result->status;
        }
        {
            const HarmonicModel baseline_model = model;
            const OrthogonalFitResult baseline_fit = fit;

            /* 弱谐波增强失败时保留可靠强峰结果，不把整帧降级为错误。 */
            if (!WeakHarmonicProbe_Augment(
                    g_fit_samples,
                    sample_rate_Hz,
                    &model,
                    &fit))
            {
                model = baseline_model;
                fit = baseline_fit;
            }
        }
    }

    result->residual_rms_V = (float)fit.residual_rms_V;
    result->snr_dB = (float)fit.snr_dB;
    if (!(calibrated
        ? SignalMetrics_PopulateCalibrated(&model, &fit, result)
        : SignalMetrics_Populate(&model, &fit, result)))
    {
        result->status = SIGNAL_STATUS_MODEL_INVALID;
        return result->status;
    }

    if (SignalAnalyzer_HasInterference(&model))
    {
        result->quality_flags |= SIGNAL_QUALITY_INTERFERENCE_REMOVED;
    }

    SignalAnalyzer_ApplySampleRateStatus(
        sample_rate_Hz,
        &model,
        result);
    return result->status;
}

SignalStatus SignalAnalyzer_Process(
    const uint16_t samples[SIGNAL_FRAME_SAMPLES],
    uint32_t sample_rate_Hz,
    SignalResult *result)
{
    return SignalAnalyzer_ProcessInternal(samples, sample_rate_Hz, result, false);
}

SignalStatus SignalAnalyzer_ProcessCalibrated(
    const uint16_t samples[SIGNAL_FRAME_SAMPLES],
    uint32_t sample_rate_Hz,
    SignalResult *result)
{
    return SignalAnalyzer_ProcessInternal(samples, sample_rate_Hz, result, true);
}
