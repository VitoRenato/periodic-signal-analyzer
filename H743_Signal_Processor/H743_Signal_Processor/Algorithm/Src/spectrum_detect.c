#include "spectrum_detect.h"
#include "signal_fft.h"

#include <math.h>
#include <stddef.h>

#define SIGNAL_SPECTRUM_PI 3.14159265358979323846
#define SIGNAL_SPECTRUM_MIN_POWER 1.0e-9f
#define SIGNAL_SPECTRUM_MIN_NOISE_POWER 1.0e-20f
#define SIGNAL_SPECTRUM_DUPLICATE_RADIUS_BINS 2U

#ifndef SIGNAL_SPECTRUM_MIN_SNR_DB
#define SIGNAL_SPECTRUM_MIN_SNR_DB 12.0f
#endif

#ifndef SIGNAL_SPECTRUM_DYNAMIC_RANGE_DB
#define SIGNAL_SPECTRUM_DYNAMIC_RANGE_DB 30.0f
#endif

static SignalFftComplex g_spectrum_fft[SIGNAL_FRAME_SAMPLES];
static float g_spectrum_power[(SIGNAL_FRAME_SAMPLES / 2U) + 1U];
static float g_noise_power[(SIGNAL_FRAME_SAMPLES / 2U) + 1U];

static size_t SpectrumDetect_LowerBin(
    uint32_t sample_rate_Hz,
    float minimum_frequency_Hz)
{
    size_t bin = (size_t)ceil(
        (double)minimum_frequency_Hz * (double)SIGNAL_FRAME_SAMPLES
        / (double)sample_rate_Hz);

    if (bin < 1U)
    {
        bin = 1U;
    }

    return bin;
}

static size_t SpectrumDetect_UpperBin(
    uint32_t sample_rate_Hz,
    float maximum_frequency_Hz)
{
    size_t bin = (size_t)floor(
        (double)maximum_frequency_Hz * (double)SIGNAL_FRAME_SAMPLES
        / (double)sample_rate_Hz);
    const size_t nyquist_bin = SIGNAL_FRAME_SAMPLES / 2U;

    if (bin > nyquist_bin)
    {
        bin = nyquist_bin;
    }

    return bin;
}

static int SpectrumDetect_IsLocalPeak(size_t bin)
{
    const size_t nyquist_bin = SIGNAL_FRAME_SAMPLES / 2U;
    const float center = g_spectrum_power[bin];
    const float left = (bin > 0U)
        ? g_spectrum_power[bin - 1U]
        : 0.0f;
    const float right = (bin < nyquist_bin)
        ? g_spectrum_power[bin + 1U]
        : 0.0f;

    return (center > left) && (center >= right);
}

static float SpectrumDetect_Median(float values[], size_t count)
{
    size_t gap;

    if (count == 0U)
    {
        return 0.0f;
    }

    for (gap = count / 2U; gap > 0U; gap /= 2U)
    {
        size_t index;

        for (index = gap; index < count; ++index)
        {
            const float value = values[index];
            size_t position = index;

            while ((position >= gap)
                && (values[position - gap] > value))
            {
                values[position] = values[position - gap];
                position -= gap;
            }
            values[position] = value;
        }
    }

    if ((count & 1U) != 0U)
    {
        return values[count / 2U];
    }

    return 0.5f
        * (values[(count / 2U) - 1U] + values[count / 2U]);
}

static void SpectrumDetect_InsertByPower(
    const SpectrumCandidate *candidate,
    SpectrumCandidate candidates[SIGNAL_MAX_CANDIDATES],
    size_t *candidate_count)
{
    size_t index;

    for (index = 0U; index < *candidate_count; ++index)
    {
        const size_t existing_bin = (size_t)candidates[index].bin;
        const size_t candidate_bin = (size_t)candidate->bin;
        const size_t distance = (existing_bin > candidate_bin)
            ? (existing_bin - candidate_bin)
            : (candidate_bin - existing_bin);

        if (distance <= SIGNAL_SPECTRUM_DUPLICATE_RADIUS_BINS)
        {
            if (candidate->power > candidates[index].power)
            {
                candidates[index] = *candidate;
            }
            return;
        }
    }

    if (*candidate_count < SIGNAL_MAX_CANDIDATES)
    {
        candidates[*candidate_count] = *candidate;
        ++(*candidate_count);
        return;
    }

    {
        size_t weakest_index = 0U;

        for (index = 1U; index < *candidate_count; ++index)
        {
            if (candidates[index].power < candidates[weakest_index].power)
            {
                weakest_index = index;
            }
        }

        if (candidate->power > candidates[weakest_index].power)
        {
            candidates[weakest_index] = *candidate;
        }
    }
}

static void SpectrumDetect_SortByFrequency(
    SpectrumCandidate candidates[SIGNAL_MAX_CANDIDATES],
    size_t candidate_count)
{
    size_t index;

    for (index = 1U; index < candidate_count; ++index)
    {
        const SpectrumCandidate candidate = candidates[index];
        size_t position = index;

        while ((position > 0U)
            && (candidates[position - 1U].bin > candidate.bin))
        {
            candidates[position] = candidates[position - 1U];
            --position;
        }
        candidates[position] = candidate;
    }
}

size_t SpectrumDetect_Find(
    const float samples[SIGNAL_FRAME_SAMPLES],
    uint32_t sample_rate_Hz,
    float minimum_frequency_Hz,
    float maximum_frequency_Hz,
    SpectrumCandidate candidates[SIGNAL_MAX_CANDIDATES])
{
    const float snr_ratio = powf(
        10.0f,
        SIGNAL_SPECTRUM_MIN_SNR_DB / 10.0f);
    const float dynamic_ratio = powf(
        10.0f,
        -SIGNAL_SPECTRUM_DYNAMIC_RANGE_DB / 10.0f);
    size_t candidate_count = 0U;
    size_t lower_bin;
    size_t upper_bin;
    size_t noise_count = 0U;
    float maximum_peak_power = 0.0f;
    float noise_floor;
    float threshold;
    size_t index;

    if ((samples == NULL)
        || (candidates == NULL)
        || (sample_rate_Hz == 0U)
        || !isfinite(minimum_frequency_Hz)
        || !isfinite(maximum_frequency_Hz)
        || (minimum_frequency_Hz < 0.0f)
        || (maximum_frequency_Hz <= minimum_frequency_Hz))
    {
        return 0U;
    }

    lower_bin = SpectrumDetect_LowerBin(
        sample_rate_Hz,
        minimum_frequency_Hz);
    upper_bin = SpectrumDetect_UpperBin(
        sample_rate_Hz,
        maximum_frequency_Hz);
    if (lower_bin > upper_bin)
    {
        return 0U;
    }

    for (index = 0U; index < SIGNAL_MAX_CANDIDATES; ++index)
    {
        candidates[index] = (SpectrumCandidate){0};
    }

    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        const double window = 0.5 - (0.5 * cos(
            2.0 * SIGNAL_SPECTRUM_PI * (double)index
            / (double)(SIGNAL_FRAME_SAMPLES - 1U)));

        g_spectrum_fft[index].real = (float)(
            (double)samples[index] * window);
        g_spectrum_fft[index].imag = 0.0f;
    }

    SignalFft_Forward4096(g_spectrum_fft);

    for (index = 0U; index <= (SIGNAL_FRAME_SAMPLES / 2U); ++index)
    {
        const double real = (double)g_spectrum_fft[index].real;
        const double imag = (double)g_spectrum_fft[index].imag;
        const double scale = (double)SIGNAL_FRAME_SAMPLES
            * (double)SIGNAL_FRAME_SAMPLES;

        g_spectrum_power[index] = (float)(
            ((real * real) + (imag * imag)) / scale);
    }

    for (index = lower_bin; index <= upper_bin; ++index)
    {
        if (SpectrumDetect_IsLocalPeak(index))
        {
            if (g_spectrum_power[index] > maximum_peak_power)
            {
                maximum_peak_power = g_spectrum_power[index];
            }
        }
        else
        {
            g_noise_power[noise_count] = g_spectrum_power[index];
            ++noise_count;
        }
    }

    if (maximum_peak_power < SIGNAL_SPECTRUM_MIN_POWER)
    {
        return 0U;
    }

    noise_floor = SpectrumDetect_Median(g_noise_power, noise_count);
    if (noise_floor < SIGNAL_SPECTRUM_MIN_NOISE_POWER)
    {
        noise_floor = SIGNAL_SPECTRUM_MIN_NOISE_POWER;
    }

    threshold = noise_floor * snr_ratio;
    if (threshold < (maximum_peak_power * dynamic_ratio))
    {
        threshold = maximum_peak_power * dynamic_ratio;
    }
    if (threshold < SIGNAL_SPECTRUM_MIN_POWER)
    {
        threshold = SIGNAL_SPECTRUM_MIN_POWER;
    }

    for (index = lower_bin; index <= upper_bin; ++index)
    {
        if (SpectrumDetect_IsLocalPeak(index)
            && (g_spectrum_power[index] >= threshold))
        {
            const SpectrumCandidate candidate = {
                (uint16_t)index,
                (float)((double)index * (double)sample_rate_Hz
                    / (double)SIGNAL_FRAME_SAMPLES),
                g_spectrum_power[index],
                10.0f * log10f(g_spectrum_power[index] / noise_floor)
            };

            SpectrumDetect_InsertByPower(
                &candidate,
                candidates,
                &candidate_count);
        }
    }

    SpectrumDetect_SortByFrequency(candidates, candidate_count);
    return candidate_count;
}