#include "orthogonal_fit.h"

#include <float.h>
#include <math.h>
#include <stddef.h>

#define ORTHOGONAL_FIT_PI 3.14159265358979323846
#define ORTHOGONAL_FIT_MAX_COLUMNS (1U + (2U * SIGNAL_MAX_FIT_TONES))
#define ORTHOGONAL_FIT_PIVOT_RATIO_MIN 1.0e-10
#define ORTHOGONAL_FIT_POWER_FLOOR 1.0e-30

static int OrthogonalFit_ValidateModel(
    uint32_t sample_rate_Hz,
    const HarmonicModel *model)
{
    const double nyquist_Hz = 0.5 * (double)sample_rate_Hz;
    size_t tone_index;
    size_t desired_count = 0U;

    if ((sample_rate_Hz == 0U)
        || (model == NULL)
        || (model->tone_count == 0U)
        || (model->tone_count > SIGNAL_MAX_FIT_TONES))
    {
        return 0;
    }

    for (tone_index = 0U;
         tone_index < (size_t)model->tone_count;
         ++tone_index)
    {
        const double frequency_Hz =
            (double)model->frequency_Hz[tone_index];

        if (!isfinite(frequency_Hz)
            || (frequency_Hz <= 0.0)
            || (frequency_Hz >= nyquist_Hz)
            || (model->desired[tone_index] > 1U))
        {
            return 0;
        }
        if (model->desired[tone_index] != 0U)
        {
            ++desired_count;
        }
    }

    return (desired_count > 0U)
        && (desired_count <= SIGNAL_MAX_COMPONENTS);
}

static int OrthogonalFit_BuildEquations(
    const float samples[SIGNAL_FRAME_SAMPLES],
    const uint8_t valid_mask[SIGNAL_FRAME_SAMPLES],
    uint32_t sample_rate_Hz,
    const HarmonicModel *model,
    double augmented[ORTHOGONAL_FIT_MAX_COLUMNS]
                    [ORTHOGONAL_FIT_MAX_COLUMNS + 1U],
    size_t column_count,
    uint32_t *fitted_sample_count)
{
    size_t sample_index;

    *fitted_sample_count = 0U;

    for (sample_index = 0U;
         sample_index < SIGNAL_FRAME_SAMPLES;
         ++sample_index)
    {
        double basis[ORTHOGONAL_FIT_MAX_COLUMNS] = {0};
        const double sample = (double)samples[sample_index];
        size_t tone_index;
        size_t row;

        if ((valid_mask != NULL) && (valid_mask[sample_index] == 0U))
        {
            continue;
        }

        if (!isfinite(sample))
        {
            return 0;
        }

        ++(*fitted_sample_count);

        basis[0U] = 1.0;
        for (tone_index = 0U;
             tone_index < (size_t)model->tone_count;
             ++tone_index)
        {
            const double angle = 2.0 * ORTHOGONAL_FIT_PI
                * (double)model->frequency_Hz[tone_index]
                * (double)sample_index / (double)sample_rate_Hz;
            const size_t cosine_column = 1U + (2U * tone_index);

            basis[cosine_column] = cos(angle);
            basis[cosine_column + 1U] = sin(angle);
        }

        for (row = 0U; row < column_count; ++row)
        {
            size_t column;

            augmented[row][column_count] += basis[row] * sample;
            for (column = 0U; column <= row; ++column)
            {
                augmented[row][column] += basis[row] * basis[column];
            }
        }
    }

    if (*fitted_sample_count < (uint32_t)(4U * column_count))
    {
        return 0;
    }

    {
        size_t row;

        for (row = 0U; row < column_count; ++row)
        {
            size_t column;

            for (column = row + 1U; column < column_count; ++column)
            {
                augmented[row][column] = augmented[column][row];
            }
        }
    }

    return 1;
}

static int OrthogonalFit_Solve(
    double augmented[ORTHOGONAL_FIT_MAX_COLUMNS]
                    [ORTHOGONAL_FIT_MAX_COLUMNS + 1U],
    size_t column_count,
    double coefficients[ORTHOGONAL_FIT_MAX_COLUMNS],
    double *pivot_ratio)
{
    double smallest_pivot = DBL_MAX;
    double largest_pivot = 0.0;
    size_t pivot_index;

    *pivot_ratio = 0.0;
    for (pivot_index = 0U;
         pivot_index < column_count;
         ++pivot_index)
    {
        size_t best_row = pivot_index;
        double best_abs = fabs(augmented[pivot_index][pivot_index]);
        size_t row;

        for (row = pivot_index + 1U; row < column_count; ++row)
        {
            const double candidate_abs =
                fabs(augmented[row][pivot_index]);

            if (candidate_abs > best_abs)
            {
                best_abs = candidate_abs;
                best_row = row;
            }
        }

        if (!isfinite(best_abs) || (best_abs <= DBL_MIN))
        {
            return 0;
        }

        if (best_row != pivot_index)
        {
            size_t column;

            for (column = 0U; column <= column_count; ++column)
            {
                const double temporary = augmented[pivot_index][column];

                augmented[pivot_index][column] = augmented[best_row][column];
                augmented[best_row][column] = temporary;
            }
        }

        best_abs = fabs(augmented[pivot_index][pivot_index]);
        if (best_abs < smallest_pivot)
        {
            smallest_pivot = best_abs;
        }
        if (best_abs > largest_pivot)
        {
            largest_pivot = best_abs;
        }

        for (row = pivot_index + 1U; row < column_count; ++row)
        {
            const double factor = augmented[row][pivot_index]
                / augmented[pivot_index][pivot_index];
            size_t column;

            if (!isfinite(factor))
            {
                return 0;
            }
            augmented[row][pivot_index] = 0.0;
            for (column = pivot_index + 1U;
                 column <= column_count;
                 ++column)
            {
                augmented[row][column] -=
                    factor * augmented[pivot_index][column];
            }
        }
    }

    if ((largest_pivot <= 0.0)
        || !isfinite(smallest_pivot)
        || !isfinite(largest_pivot))
    {
        return 0;
    }

    *pivot_ratio = smallest_pivot / largest_pivot;
    if (!isfinite(*pivot_ratio)
        || (*pivot_ratio < ORTHOGONAL_FIT_PIVOT_RATIO_MIN))
    {
        return 0;
    }

    for (pivot_index = column_count; pivot_index-- > 0U;)
    {
        double value = augmented[pivot_index][column_count];
        size_t column;

        for (column = pivot_index + 1U; column < column_count; ++column)
        {
            value -= augmented[pivot_index][column] * coefficients[column];
        }
        coefficients[pivot_index] =
            value / augmented[pivot_index][pivot_index];
        if (!isfinite(coefficients[pivot_index]))
        {
            return 0;
        }
    }

    return 1;
}

static double OrthogonalFit_NormalizePhase(double phase_rad)
{
    while (phase_rad >= ORTHOGONAL_FIT_PI)
    {
        phase_rad -= 2.0 * ORTHOGONAL_FIT_PI;
    }
    while (phase_rad < -ORTHOGONAL_FIT_PI)
    {
        phase_rad += 2.0 * ORTHOGONAL_FIT_PI;
    }
    return phase_rad;
}

static int OrthogonalFit_CalculateMetrics(
    const float samples[SIGNAL_FRAME_SAMPLES],
    const uint8_t valid_mask[SIGNAL_FRAME_SAMPLES],
    double lower_clip_threshold_V,
    uint32_t sample_rate_Hz,
    const HarmonicModel *model,
    const double coefficients[ORTHOGONAL_FIT_MAX_COLUMNS],
    OrthogonalFitResult *fit)
{
    double residual_energy = 0.0;
    double signal_energy = 0.0;
    double violation_energy = 0.0;
    uint32_t valid_sample_count = 0U;
    uint32_t clipped_sample_count = 0U;
    size_t sample_index;

    for (sample_index = 0U;
         sample_index < SIGNAL_FRAME_SAMPLES;
         ++sample_index)
    {
        double fitted_sample = coefficients[0U];
        size_t tone_index;

        for (tone_index = 0U;
             tone_index < (size_t)model->tone_count;
             ++tone_index)
        {
            const double angle = 2.0 * ORTHOGONAL_FIT_PI
                * (double)model->frequency_Hz[tone_index]
                * (double)sample_index / (double)sample_rate_Hz;
            const size_t cosine_column = 1U + (2U * tone_index);

            fitted_sample += coefficients[cosine_column] * cos(angle)
                + coefficients[cosine_column + 1U] * sin(angle);
        }

        if ((valid_mask != NULL) && (valid_mask[sample_index] == 0U))
        {
            const double violation = fitted_sample - lower_clip_threshold_V;

            ++clipped_sample_count;
            if (violation > 0.0)
            {
                ++fit->clip_violation_count;
                violation_energy += violation * violation;
            }
            continue;
        }

        {
            const double sample = (double)samples[sample_index];
            const double residual = sample - fitted_sample;
            const double ac_sample = sample - coefficients[0U];

            residual_energy += residual * residual;
            signal_energy += ac_sample * ac_sample;
            ++valid_sample_count;
        }
    }

    if (valid_sample_count == 0U)
    {
        return 0;
    }

    {
        const double residual_power = residual_energy
            / (double)valid_sample_count;
        const double signal_power = signal_energy
            / (double)valid_sample_count;
        const double bounded_residual_power =
            (residual_power > ORTHOGONAL_FIT_POWER_FLOOR)
                ? residual_power
                : ORTHOGONAL_FIT_POWER_FLOOR;
        const double bounded_signal_power =
            (signal_power > ORTHOGONAL_FIT_POWER_FLOOR)
                ? signal_power
                : ORTHOGONAL_FIT_POWER_FLOOR;

        fit->residual_rms_V = sqrt(residual_power);
        fit->snr_dB = 10.0 * log10(
            bounded_signal_power / bounded_residual_power);
        fit->clip_violation_rms_V = (clipped_sample_count != 0U)
            ? sqrt(violation_energy / (double)clipped_sample_count)
            : 0.0;
    }

    return isfinite(fit->residual_rms_V)
        && isfinite(fit->snr_dB)
        && isfinite(fit->clip_violation_rms_V);
}

bool OrthogonalFit_Run(
    const float samples[SIGNAL_FRAME_SAMPLES],
    uint32_t sample_rate_Hz,
    const HarmonicModel *model,
    OrthogonalFitResult *fit)
{
    return OrthogonalFit_RunMasked(
        samples,
        NULL,
        0.0,
        sample_rate_Hz,
        model,
        fit);
}

bool OrthogonalFit_RunMasked(
    const float samples[SIGNAL_FRAME_SAMPLES],
    const uint8_t valid_mask[SIGNAL_FRAME_SAMPLES],
    double lower_clip_threshold_V,
    uint32_t sample_rate_Hz,
    const HarmonicModel *model,
    OrthogonalFitResult *fit)
{
    double augmented[ORTHOGONAL_FIT_MAX_COLUMNS]
                    [ORTHOGONAL_FIT_MAX_COLUMNS + 1U] = {{0}};
    double coefficients[ORTHOGONAL_FIT_MAX_COLUMNS] = {0};
    size_t column_count;
    size_t tone_index;

    if (fit == NULL)
    {
        return false;
    }
    *fit = (OrthogonalFitResult){0};

    if ((samples == NULL)
        || !isfinite(lower_clip_threshold_V)
        || !OrthogonalFit_ValidateModel(sample_rate_Hz, model))
    {
        return false;
    }

    column_count = 1U + (2U * (size_t)model->tone_count);
    if (!OrthogonalFit_BuildEquations(
            samples,
            valid_mask,
            sample_rate_Hz,
            model,
            augmented,
            column_count,
            &fit->fitted_sample_count)
        || !OrthogonalFit_Solve(
            augmented,
            column_count,
            coefficients,
            &fit->pivot_ratio))
    {
        return false;
    }

    fit->dc_V = coefficients[0U];
    for (tone_index = 0U;
         tone_index < (size_t)model->tone_count;
         ++tone_index)
    {
        const size_t cosine_column = 1U + (2U * tone_index);
        const double cosine_coefficient = coefficients[cosine_column];
        const double sine_coefficient = coefficients[cosine_column + 1U];
        const double amplitude_peak_V = hypot(
            cosine_coefficient,
            sine_coefficient);
        const double phase_rad = OrthogonalFit_NormalizePhase(
            atan2(-sine_coefficient, cosine_coefficient));

        if (!isfinite(amplitude_peak_V) || !isfinite(phase_rad))
        {
            return false;
        }
        fit->tone[tone_index].frequency_Hz =
            model->frequency_Hz[tone_index];
        fit->tone[tone_index].amplitude_peak_V =
            (float)amplitude_peak_V;
        fit->tone[tone_index].phase_rad = (float)phase_rad;
    }

    return OrthogonalFit_CalculateMetrics(
        samples,
        valid_mask,
        lower_clip_threshold_V,
        sample_rate_Hz,
        model,
        coefficients,
        fit) != 0;
}
