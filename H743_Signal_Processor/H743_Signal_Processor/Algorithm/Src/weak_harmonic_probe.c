#include "weak_harmonic_probe.h"
#include "ad9226_calibration.h"

#include <float.h>
#include <math.h>
#include <stddef.h>

#define WEAK_HARMONIC_PI 3.14159265358979323846
#define WEAK_HARMONIC_MIN_ORDER 2U
#define WEAK_HARMONIC_MAX_ORDER 50U
#define WEAK_HARMONIC_MAX_FREQUENCY_HZ 500000.0
#define WEAK_HARMONIC_SHORTLIST_COUNT 4U
#define WEAK_HARMONIC_SIGMA_MULTIPLIER 4.0
#define WEAK_HARMONIC_MIN_RELATIVE_AMPLITUDE 0.01
#define WEAK_HARMONIC_MIN_BIC_IMPROVEMENT 10.0
#define WEAK_HARMONIC_MIN_AMPLITUDE_V \
    (0.5 * (double)AD9226_BNC_V_PER_CODE)
#define WEAK_HARMONIC_FREQUENCY_TOLERANCE_HZ 1000.0
#define WEAK_HARMONIC_POWER_FLOOR 1.0e-30

typedef struct
{
    unsigned int order;
    double frequency_Hz;
    double amplitude_V;
    double amplitude_standard_error_V;
    double significance;
} WeakHarmonicCandidate;

static double g_weak_harmonic_residual[SIGNAL_FRAME_SAMPLES];

static size_t WeakHarmonicProbe_DesiredCount(const HarmonicModel *model)
{
    size_t count = 0U;
    size_t index;

    for (index = 0U; index < (size_t)model->tone_count; ++index)
    {
        if (model->desired[index] != 0U)
        {
            ++count;
        }
    }
    return count;
}

static int WeakHarmonicProbe_GetFundamental(
    const HarmonicModel *model,
    const OrthogonalFitResult *fit,
    double *frequency_Hz,
    double *amplitude_V)
{
    size_t index;

    for (index = 0U; index < (size_t)model->tone_count; ++index)
    {
        if (model->desired[index] != 0U)
        {
            *frequency_Hz = (double)model->frequency_Hz[index];
            *amplitude_V = (double)fit->tone[index].amplitude_peak_V;
            return isfinite(*frequency_Hz)
                && isfinite(*amplitude_V)
                && (*frequency_Hz > 0.0)
                && (*amplitude_V > 0.0);
        }
    }
    return 0;
}

static int WeakHarmonicProbe_ReconstructResidual(
    const float samples[SIGNAL_FRAME_SAMPLES],
    uint32_t sample_rate_Hz,
    const HarmonicModel *model,
    const OrthogonalFitResult *fit)
{
    size_t sample_index;

    if (!isfinite(fit->dc_V))
    {
        return 0;
    }

    for (sample_index = 0U;
         sample_index < SIGNAL_FRAME_SAMPLES;
         ++sample_index)
    {
        double predicted = fit->dc_V;
        size_t tone_index;

        if (!isfinite(samples[sample_index]))
        {
            return 0;
        }
        for (tone_index = 0U;
             tone_index < (size_t)model->tone_count;
             ++tone_index)
        {
            const double frequency_Hz =
                (double)model->frequency_Hz[tone_index];
            const double amplitude_V =
                (double)fit->tone[tone_index].amplitude_peak_V;
            const double phase_rad =
                (double)fit->tone[tone_index].phase_rad;
            const double angle = 2.0 * WEAK_HARMONIC_PI
                * frequency_Hz * (double)sample_index
                / (double)sample_rate_Hz;

            if (!isfinite(frequency_Hz)
                || !isfinite(amplitude_V)
                || !isfinite(phase_rad))
            {
                return 0;
            }
            predicted += amplitude_V * cos(angle + phase_rad);
        }
        g_weak_harmonic_residual[sample_index] =
            (double)samples[sample_index] - predicted;
        if (!isfinite(g_weak_harmonic_residual[sample_index]))
        {
            return 0;
        }
    }
    return 1;
}

static int WeakHarmonicProbe_OrderExists(
    const HarmonicModel *model,
    double candidate_frequency_Hz)
{
    size_t index;

    for (index = 0U; index < (size_t)model->tone_count; ++index)
    {
        if ((model->desired[index] != 0U)
            && (fabs((double)model->frequency_Hz[index]
                - candidate_frequency_Hz)
                <= WEAK_HARMONIC_FREQUENCY_TOLERANCE_HZ))
        {
            return 1;
        }
    }
    return 0;
}

static int WeakHarmonicProbe_ProjectCandidate(
    double frequency_Hz,
    uint32_t sample_rate_Hz,
    double sigma2,
    WeakHarmonicCandidate *candidate)
{
    const double angle_step = 2.0 * WEAK_HARMONIC_PI
        * frequency_Hz / (double)sample_rate_Hz;
    const double cosine_step = cos(angle_step);
    const double sine_step = sin(angle_step);
    double cosine_value = 1.0;
    double sine_value = 0.0;
    double g00 = 0.0;
    double g01 = 0.0;
    double g11 = 0.0;
    double b0 = 0.0;
    double b1 = 0.0;
    double determinant;
    double cosine_coefficient;
    double sine_coefficient;
    double variance_cosine;
    double variance_sine;
    double covariance;
    double amplitude_variance;
    size_t sample_index;

    for (sample_index = 0U;
         sample_index < SIGNAL_FRAME_SAMPLES;
         ++sample_index)
    {
        const double residual = g_weak_harmonic_residual[sample_index];
        const double next_cosine = cosine_value * cosine_step
            - sine_value * sine_step;
        const double next_sine = sine_value * cosine_step
            + cosine_value * sine_step;

        g00 += cosine_value * cosine_value;
        g01 += cosine_value * sine_value;
        g11 += sine_value * sine_value;
        b0 += residual * cosine_value;
        b1 += residual * sine_value;
        cosine_value = next_cosine;
        sine_value = next_sine;
    }

    determinant = (g00 * g11) - (g01 * g01);
    if (!isfinite(determinant) || (determinant <= DBL_MIN))
    {
        return 0;
    }

    cosine_coefficient = ((b0 * g11) - (b1 * g01)) / determinant;
    sine_coefficient = ((g00 * b1) - (g01 * b0)) / determinant;
    candidate->amplitude_V = hypot(
        cosine_coefficient,
        sine_coefficient);
    if (!isfinite(candidate->amplitude_V)
        || (candidate->amplitude_V <= 0.0))
    {
        return 0;
    }

    variance_cosine = sigma2 * g11 / determinant;
    variance_sine = sigma2 * g00 / determinant;
    covariance = -sigma2 * g01 / determinant;
    amplitude_variance =
        ((cosine_coefficient * cosine_coefficient * variance_cosine)
        + (sine_coefficient * sine_coefficient * variance_sine)
        + (2.0 * cosine_coefficient * sine_coefficient * covariance))
        / (candidate->amplitude_V * candidate->amplitude_V);
    if (!isfinite(amplitude_variance) || (amplitude_variance <= 0.0))
    {
        return 0;
    }

    candidate->amplitude_standard_error_V = sqrt(amplitude_variance);
    candidate->significance = candidate->amplitude_V
        / candidate->amplitude_standard_error_V;
    return isfinite(candidate->amplitude_standard_error_V)
        && (candidate->amplitude_standard_error_V > 0.0)
        && isfinite(candidate->significance);
}

static void WeakHarmonicProbe_InsertShortlist(
    const WeakHarmonicCandidate *candidate,
    WeakHarmonicCandidate shortlist[WEAK_HARMONIC_SHORTLIST_COUNT],
    size_t *shortlist_count)
{
    size_t position = 0U;
    size_t move;

    while ((position < *shortlist_count)
        && (shortlist[position].significance >= candidate->significance))
    {
        ++position;
    }
    if (position >= WEAK_HARMONIC_SHORTLIST_COUNT)
    {
        return;
    }

    if (*shortlist_count < WEAK_HARMONIC_SHORTLIST_COUNT)
    {
        ++(*shortlist_count);
    }
    for (move = *shortlist_count - 1U; move > position; --move)
    {
        shortlist[move] = shortlist[move - 1U];
    }
    shortlist[position] = *candidate;
}

static size_t WeakHarmonicProbe_BuildShortlist(
    uint32_t sample_rate_Hz,
    const HarmonicModel *model,
    const OrthogonalFitResult *fit,
    double fundamental_frequency_Hz,
    double fundamental_amplitude_V,
    WeakHarmonicCandidate shortlist[WEAK_HARMONIC_SHORTLIST_COUNT])
{
    const size_t parameter_count = 1U + (2U * (size_t)model->tone_count);
    const double fitted_count = (double)fit->fitted_sample_count;
    const double residual_rms_V = fit->residual_rms_V;
    const double rss = residual_rms_V * residual_rms_V * fitted_count;
    double sigma2;
    size_t shortlist_count = 0U;
    unsigned int order;

    if ((fit->fitted_sample_count <= parameter_count)
        || !isfinite(rss)
        || (rss <= 0.0))
    {
        return 0U;
    }
    sigma2 = rss / (fitted_count - (double)parameter_count);
    if (!isfinite(sigma2) || (sigma2 <= 0.0))
    {
        return 0U;
    }

    for (order = WEAK_HARMONIC_MIN_ORDER;
         order <= WEAK_HARMONIC_MAX_ORDER;
         ++order)
    {
        WeakHarmonicCandidate candidate = {0};
        const double candidate_frequency_Hz =
            fundamental_frequency_Hz * (double)order;

        if ((candidate_frequency_Hz > WEAK_HARMONIC_MAX_FREQUENCY_HZ)
            || (candidate_frequency_Hz
                >= (0.5 * (double)sample_rate_Hz)))
        {
            break;
        }
        if (WeakHarmonicProbe_OrderExists(model, candidate_frequency_Hz))
        {
            continue;
        }

        candidate.order = order;
        candidate.frequency_Hz = candidate_frequency_Hz;
        if (!WeakHarmonicProbe_ProjectCandidate(
                candidate_frequency_Hz,
                sample_rate_Hz,
                sigma2,
                &candidate)
            || (candidate.amplitude_V
                < (WEAK_HARMONIC_SIGMA_MULTIPLIER
                    * candidate.amplitude_standard_error_V))
            || (candidate.amplitude_V < WEAK_HARMONIC_MIN_AMPLITUDE_V)
            || (candidate.amplitude_V
                < (WEAK_HARMONIC_MIN_RELATIVE_AMPLITUDE
                    * fundamental_amplitude_V)))
        {
            continue;
        }
        WeakHarmonicProbe_InsertShortlist(
            &candidate,
            shortlist,
            &shortlist_count);
    }
    return shortlist_count;
}

static int WeakHarmonicProbe_InsertTone(
    const HarmonicModel *model,
    double candidate_frequency_Hz,
    HarmonicModel *trial_model,
    size_t *inserted_index)
{
    size_t input_index;
    size_t output_index = 0U;
    int inserted = 0;

    if (model->tone_count >= SIGNAL_MAX_FIT_TONES)
    {
        return 0;
    }
    *trial_model = (HarmonicModel){0};

    for (input_index = 0U;
         input_index < (size_t)model->tone_count;
         ++input_index)
    {
        if (!inserted
            && ((model->desired[input_index] == 0U)
                || (candidate_frequency_Hz
                    < (double)model->frequency_Hz[input_index])))
        {
            *inserted_index = output_index;
            trial_model->frequency_Hz[output_index] =
                (float)candidate_frequency_Hz;
            trial_model->desired[output_index] = 1U;
            ++output_index;
            inserted = 1;
        }
        trial_model->frequency_Hz[output_index] =
            model->frequency_Hz[input_index];
        trial_model->desired[output_index] = model->desired[input_index];
        ++output_index;
    }
    if (!inserted)
    {
        *inserted_index = output_index;
        trial_model->frequency_Hz[output_index] =
            (float)candidate_frequency_Hz;
        trial_model->desired[output_index] = 1U;
        ++output_index;
    }
    trial_model->tone_count = (uint8_t)output_index;
    return 1;
}

static double WeakHarmonicProbe_Bic(
    const HarmonicModel *model,
    const OrthogonalFitResult *fit)
{
    const double count = (double)fit->fitted_sample_count;
    const double residual_rms_V = fit->residual_rms_V;
    double mean_residual_power = residual_rms_V * residual_rms_V;
    const double parameter_count =
        (double)(1U + (2U * (size_t)model->tone_count));

    if (mean_residual_power < WEAK_HARMONIC_POWER_FLOOR)
    {
        mean_residual_power = WEAK_HARMONIC_POWER_FLOOR;
    }
    if ((count <= parameter_count)
        || !isfinite(mean_residual_power)
        || !isfinite(count))
    {
        return INFINITY;
    }
    return count * log(mean_residual_power)
        + parameter_count * log(count);
}

bool WeakHarmonicProbe_Augment(
    const float samples[SIGNAL_FRAME_SAMPLES],
    uint32_t sample_rate_Hz,
    HarmonicModel *model,
    OrthogonalFitResult *fit)
{
    double fundamental_frequency_Hz;
    double fundamental_amplitude_V;

    if ((samples == NULL)
        || (model == NULL)
        || (fit == NULL)
        || (sample_rate_Hz == 0U)
        || (model->tone_count == 0U)
        || (model->tone_count > SIGNAL_MAX_FIT_TONES)
        || !WeakHarmonicProbe_GetFundamental(
            model,
            fit,
            &fundamental_frequency_Hz,
            &fundamental_amplitude_V))
    {
        return false;
    }

    while ((WeakHarmonicProbe_DesiredCount(model) < SIGNAL_MAX_COMPONENTS)
        && (model->tone_count < SIGNAL_MAX_FIT_TONES))
    {
        WeakHarmonicCandidate shortlist[WEAK_HARMONIC_SHORTLIST_COUNT] = {{0}};
        HarmonicModel best_model = *model;
        OrthogonalFitResult best_fit = *fit;
        const double baseline_bic = WeakHarmonicProbe_Bic(model, fit);
        double best_improvement = WEAK_HARMONIC_MIN_BIC_IMPROVEMENT;
        size_t shortlist_count;
        size_t candidate_index;
        int accepted = 0;

        if (!isfinite(baseline_bic)
            || !WeakHarmonicProbe_ReconstructResidual(
                samples,
                sample_rate_Hz,
                model,
                fit))
        {
            return false;
        }
        shortlist_count = WeakHarmonicProbe_BuildShortlist(
            sample_rate_Hz,
            model,
            fit,
            fundamental_frequency_Hz,
            fundamental_amplitude_V,
            shortlist);

        for (candidate_index = 0U;
             candidate_index < shortlist_count;
             ++candidate_index)
        {
            HarmonicModel trial_model;
            OrthogonalFitResult trial_fit;
            size_t inserted_index;
            double trial_bic;
            double improvement;
            double fitted_amplitude_V;

            if (!WeakHarmonicProbe_InsertTone(
                    model,
                    shortlist[candidate_index].frequency_Hz,
                    &trial_model,
                    &inserted_index)
                || !OrthogonalFit_Run(
                    samples,
                    sample_rate_Hz,
                    &trial_model,
                    &trial_fit))
            {
                continue;
            }

            fitted_amplitude_V =
                (double)trial_fit.tone[inserted_index].amplitude_peak_V;
            trial_bic = WeakHarmonicProbe_Bic(&trial_model, &trial_fit);
            improvement = baseline_bic - trial_bic;
            if (!isfinite(fitted_amplitude_V)
                || (fitted_amplitude_V
                    < (WEAK_HARMONIC_SIGMA_MULTIPLIER
                        * shortlist[candidate_index]
                            .amplitude_standard_error_V))
                || (fitted_amplitude_V < WEAK_HARMONIC_MIN_AMPLITUDE_V)
                || (fitted_amplitude_V
                    < (WEAK_HARMONIC_MIN_RELATIVE_AMPLITUDE
                        * fundamental_amplitude_V))
                || !isfinite(improvement)
                || (improvement < best_improvement)
                || !isfinite(trial_fit.pivot_ratio)
                || (trial_fit.pivot_ratio < 1.0e-10))
            {
                continue;
            }

            best_improvement = improvement;
            best_model = trial_model;
            best_fit = trial_fit;
            accepted = 1;
        }

        if (!accepted)
        {
            break;
        }
        *model = best_model;
        *fit = best_fit;
    }
    return true;
}
