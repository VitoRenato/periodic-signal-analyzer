#include "signal_metrics.h"

#include <float.h>
#include <math.h>
#include <stddef.h>

#define SIGNAL_METRICS_PI 3.14159265358979323846
#define SIGNAL_METRICS_RECONSTRUCTION_POINTS 8192U
#define SIGNAL_METRICS_HARMONIC_ERROR_HZ 1000.0

static void SignalMetrics_SortComponents(
    SignalComponent component[SIGNAL_MAX_COMPONENTS],
    size_t component_count)
{
    size_t index;

    for (index = 1U; index < component_count; ++index)
    {
        const SignalComponent value = component[index];
        size_t position = index;

        while ((position > 0U)
            && (component[position - 1U].frequency_Hz
                > value.frequency_Hz))
        {
            component[position] = component[position - 1U];
            --position;
        }
        component[position] = value;
    }
}

static int SignalMetrics_CollectDesired(
    const HarmonicModel *model,
    const OrthogonalFitResult *fit,
    SignalComponent component[SIGNAL_MAX_COMPONENTS],
    size_t *component_count)
{
    size_t tone_index;

    *component_count = 0U;
    if ((model->tone_count == 0U)
        || (model->tone_count > SIGNAL_MAX_FIT_TONES))
    {
        return 0;
    }

    for (tone_index = 0U;
         tone_index < (size_t)model->tone_count;
         ++tone_index)
    {
        const SignalComponent tone = fit->tone[tone_index];

        if (model->desired[tone_index] > 1U)
        {
            return 0;
        }
        if (model->desired[tone_index] == 0U)
        {
            continue;
        }
        if ((*component_count >= SIGNAL_MAX_COMPONENTS)
            || !isfinite(tone.frequency_Hz)
            || !isfinite(tone.amplitude_peak_V)
            || !isfinite(tone.phase_rad)
            || (tone.frequency_Hz <= 0.0f)
            || (tone.amplitude_peak_V < 0.0f))
        {
            return 0;
        }

        component[*component_count] = tone;
        ++(*component_count);
    }

    if (*component_count == 0U)
    {
        return 0;
    }
    SignalMetrics_SortComponents(component, *component_count);
    return 1;
}

static int SignalMetrics_GetHarmonicOrders(
    const SignalComponent component[SIGNAL_MAX_COMPONENTS],
    size_t component_count,
    unsigned int order[SIGNAL_MAX_COMPONENTS])
{
    const double fundamental_Hz = (double)component[0U].frequency_Hz;
    size_t component_index;

    for (component_index = 0U;
         component_index < component_count;
         ++component_index)
    {
        const double frequency_Hz =
            (double)component[component_index].frequency_Hz;
        const double ratio = frequency_Hz / fundamental_Hz;
        const double rounded_order = floor(ratio + 0.5);
        size_t previous_index;

        if (!isfinite(ratio)
            || (rounded_order < 1.0)
            || (rounded_order > 50.0)
            || (fabs(frequency_Hz
                - (rounded_order * fundamental_Hz))
                > SIGNAL_METRICS_HARMONIC_ERROR_HZ))
        {
            return 0;
        }

        order[component_index] = (unsigned int)rounded_order;
        for (previous_index = 0U;
             previous_index < component_index;
             ++previous_index)
        {
            if (order[previous_index] == order[component_index])
            {
                return 0;
            }
        }
    }

    return order[0U] == 1U;
}

static int SignalMetrics_CalculateValues(
    const SignalComponent component[SIGNAL_MAX_COMPONENTS],
    const unsigned int order[SIGNAL_MAX_COMPONENTS],
    size_t component_count,
    double *upp_V,
    double *urms_V)
{
    double minimum_V = DBL_MAX;
    double maximum_V = -DBL_MAX;
    double rms_power_V2 = 0.0;
    size_t component_index;
    size_t phase_index;

    for (component_index = 0U;
         component_index < component_count;
         ++component_index)
    {
        const double amplitude_V =
            (double)component[component_index].amplitude_peak_V;

        rms_power_V2 += 0.5 * amplitude_V * amplitude_V;
    }

    for (phase_index = 0U;
         phase_index < SIGNAL_METRICS_RECONSTRUCTION_POINTS;
         ++phase_index)
    {
        const double fundamental_phase = 2.0 * SIGNAL_METRICS_PI
            * (double)phase_index
            / (double)SIGNAL_METRICS_RECONSTRUCTION_POINTS;
        double sample_V = 0.0;

        for (component_index = 0U;
             component_index < component_count;
             ++component_index)
        {
            sample_V +=
                (double)component[component_index].amplitude_peak_V
                * cos(((double)order[component_index]
                    * fundamental_phase)
                    + (double)component[component_index].phase_rad);
        }

        if (sample_V < minimum_V)
        {
            minimum_V = sample_V;
        }
        if (sample_V > maximum_V)
        {
            maximum_V = sample_V;
        }
    }

    *upp_V = maximum_V - minimum_V;
    *urms_V = sqrt(rms_power_V2);
    return isfinite(*upp_V)
        && isfinite(*urms_V)
        && (*upp_V >= 0.0)
        && (*urms_V >= 0.0)
        && (*upp_V <= (double)FLT_MAX)
        && (*urms_V <= (double)FLT_MAX);
}

bool SignalMetrics_Populate(
    const HarmonicModel *model,
    const OrthogonalFitResult *fit,
    SignalResult *result)
{
    SignalComponent component[SIGNAL_MAX_COMPONENTS] = {{0}};
    unsigned int order[SIGNAL_MAX_COMPONENTS] = {0U};
    size_t component_count;
    double upp_V;
    double urms_V;
    size_t component_index;

    if ((model == NULL) || (fit == NULL) || (result == NULL))
    {
        return false;
    }
    if (!isfinite(fit->residual_rms_V)
        || !isfinite(fit->snr_dB)
        || (fit->residual_rms_V < 0.0)
        || (fabs(fit->residual_rms_V) > (double)FLT_MAX)
        || (fabs(fit->snr_dB) > (double)FLT_MAX)
        || !SignalMetrics_CollectDesired(
            model,
            fit,
            component,
            &component_count)
        || !SignalMetrics_GetHarmonicOrders(
            component,
            component_count,
            order)
        || !SignalMetrics_CalculateValues(
            component,
            order,
            component_count,
            &upp_V,
            &urms_V))
    {
        return false;
    }

    /* A strong fitted interferer must not hide a poor target-band fit. */
    if (fit->residual_rms_V * fit->residual_rms_V > 0.1 * urms_V * urms_V)
    {
        return false;
    }
    result->fundamental_Hz = component[0U].frequency_Hz;
    result->upp_V = (float)upp_V;
    result->urms_V = (float)urms_V;
    result->residual_rms_V = (float)fit->residual_rms_V;
    result->snr_dB = (float)fit->snr_dB;
    result->component_count = (uint8_t)component_count;
    for (component_index = 0U;
         component_index < SIGNAL_MAX_COMPONENTS;
         ++component_index)
    {
        result->component[component_index] =
            (component_index < component_count)
                ? component[component_index]
                : (SignalComponent){0};
    }

    return true;
}