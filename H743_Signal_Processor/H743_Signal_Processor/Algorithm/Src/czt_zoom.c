#include "czt_zoom.h"

#include <math.h>
#include <stddef.h>

#define CZT_ZOOM_POINT_COUNT 129U
#define CZT_ZOOM_INTERVAL_COUNT (CZT_ZOOM_POINT_COUNT - 1U)
#define CZT_ZOOM_HALF_WIDTH_BINS 2.0
#define CZT_ZOOM_PI 3.14159265358979323846
#define CZT_ZOOM_MIN_CURVATURE 1.0e-18

static float g_czt_windowed_samples[SIGNAL_FRAME_SAMPLES];
static double g_czt_power[CZT_ZOOM_POINT_COUNT];

static double CztZoom_EvaluatePower(
    double frequency_Hz,
    uint32_t sample_rate_Hz)
{
    const double angle_step = -2.0 * CZT_ZOOM_PI * frequency_Hz
        / (double)sample_rate_Hz;
    const double step_real = cos(angle_step);
    const double step_imag = sin(angle_step);
    double phasor_real = 1.0;
    double phasor_imag = 0.0;
    double sum_real = 0.0;
    double sum_imag = 0.0;
    size_t index;

    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        const double sample = (double)g_czt_windowed_samples[index];
        const double next_phasor_real =
            (phasor_real * step_real) - (phasor_imag * step_imag);
        const double next_phasor_imag =
            (phasor_real * step_imag) + (phasor_imag * step_real);

        sum_real += sample * phasor_real;
        sum_imag += sample * phasor_imag;
        phasor_real = next_phasor_real;
        phasor_imag = next_phasor_imag;
    }

    {
        const double normalization = (double)SIGNAL_FRAME_SAMPLES
            * (double)SIGNAL_FRAME_SAMPLES;

        return ((sum_real * sum_real) + (sum_imag * sum_imag))
            / normalization;
    }
}

static int CztZoom_PrepareWindowedSamples(
    const float samples[SIGNAL_FRAME_SAMPLES])
{
    double energy = 0.0;
    size_t index;

    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        const double sample = (double)samples[index];
        const double window = 0.5 - (0.5 * cos(
            2.0 * CZT_ZOOM_PI * (double)index
            / (double)(SIGNAL_FRAME_SAMPLES - 1U)));
        const double windowed_sample = sample * window;

        if (!isfinite(sample) || !isfinite(windowed_sample))
        {
            return 0;
        }

        g_czt_windowed_samples[index] = (float)windowed_sample;
        energy += windowed_sample * windowed_sample;
    }

    return isfinite(energy) && (energy > 0.0);
}

CztRefinedPeak CztZoom_Refine(
    const float samples[SIGNAL_FRAME_SAMPLES],
    uint32_t sample_rate_Hz,
    float coarse_frequency_Hz)
{
    CztRefinedPeak result = {0};
    const double sample_rate = (double)sample_rate_Hz;
    const double nyquist_frequency_Hz = 0.5 * sample_rate;
    const double bin_spacing_Hz = sample_rate
        / (double)SIGNAL_FRAME_SAMPLES;
    const double frequency_margin_Hz = bin_spacing_Hz
        / (double)SIGNAL_FRAME_SAMPLES;
    double start_frequency_Hz;
    double stop_frequency_Hz;
    double frequency_step_Hz;
    size_t peak_index = 0U;
    double peak_power;
    size_t index;

    if ((samples == NULL)
        || (sample_rate_Hz == 0U)
        || !isfinite(coarse_frequency_Hz)
        || (coarse_frequency_Hz <= 0.0f)
        || ((double)coarse_frequency_Hz >= nyquist_frequency_Hz))
    {
        return result;
    }

    start_frequency_Hz = (double)coarse_frequency_Hz
        - (CZT_ZOOM_HALF_WIDTH_BINS * bin_spacing_Hz);
    stop_frequency_Hz = (double)coarse_frequency_Hz
        + (CZT_ZOOM_HALF_WIDTH_BINS * bin_spacing_Hz);

    if (start_frequency_Hz <= 0.0)
    {
        start_frequency_Hz = frequency_margin_Hz;
    }
    if (stop_frequency_Hz >= nyquist_frequency_Hz)
    {
        stop_frequency_Hz = nyquist_frequency_Hz - frequency_margin_Hz;
    }
    if (!(stop_frequency_Hz > start_frequency_Hz))
    {
        return result;
    }

    if (!CztZoom_PrepareWindowedSamples(samples))
    {
        return result;
    }

    frequency_step_Hz = (stop_frequency_Hz - start_frequency_Hz)
        / (double)CZT_ZOOM_INTERVAL_COUNT;
    if (!isfinite(frequency_step_Hz) || (frequency_step_Hz <= 0.0))
    {
        return result;
    }

    for (index = 0U; index < CZT_ZOOM_POINT_COUNT; ++index)
    {
        const double frequency_Hz = start_frequency_Hz
            + ((double)index * frequency_step_Hz);

        g_czt_power[index] = CztZoom_EvaluatePower(
            frequency_Hz,
            sample_rate_Hz);
        if (!isfinite(g_czt_power[index]) || (g_czt_power[index] < 0.0))
        {
            return result;
        }
    }

    peak_power = g_czt_power[0U];
    for (index = 1U; index < CZT_ZOOM_POINT_COUNT; ++index)
    {
        if (g_czt_power[index] > peak_power)
        {
            peak_power = g_czt_power[index];
            peak_index = index;
        }
    }

    if ((peak_index == 0U)
        || (peak_index == (CZT_ZOOM_POINT_COUNT - 1U))
        || (peak_power <= 0.0))
    {
        return result;
    }

    {
        const double left_power = g_czt_power[peak_index - 1U];
        const double center_power = g_czt_power[peak_index];
        const double right_power = g_czt_power[peak_index + 1U];
        double left_log_power;
        double center_log_power;
        double right_log_power;
        double curvature;
        double offset;
        double refined_frequency_Hz;
        double refined_log_power;
        double refined_power;

        if ((left_power <= 0.0)
            || (center_power <= 0.0)
            || (right_power <= 0.0))
        {
            return result;
        }

        left_log_power = log(left_power);
        center_log_power = log(center_power);
        right_log_power = log(right_power);
        curvature = left_log_power
            - (2.0 * center_log_power)
            + right_log_power;

        if (!isfinite(left_log_power)
            || !isfinite(center_log_power)
            || !isfinite(right_log_power)
            || !isfinite(curvature)
            || (fabs(curvature) <= CZT_ZOOM_MIN_CURVATURE))
        {
            return result;
        }

        offset = 0.5 * (left_log_power - right_log_power) / curvature;
        if (offset < -0.5)
        {
            offset = -0.5;
        }
        else if (offset > 0.5)
        {
            offset = 0.5;
        }

        refined_frequency_Hz = start_frequency_Hz
            + (((double)peak_index + offset) * frequency_step_Hz);
        refined_log_power = center_log_power
            - (0.25 * (left_log_power - right_log_power) * offset);
        refined_power = exp(refined_log_power);

        if (!isfinite(refined_frequency_Hz)
            || !isfinite(refined_power)
            || (refined_power <= 0.0)
            || (refined_frequency_Hz < start_frequency_Hz)
            || (refined_frequency_Hz > stop_frequency_Hz)
            || (refined_frequency_Hz <= 0.0)
            || (refined_frequency_Hz >= nyquist_frequency_Hz))
        {
            return result;
        }

        result.frequency_Hz = (float)refined_frequency_Hz;
        result.peak_power = (float)refined_power;
        result.valid = 1U;
    }

    return result;
}