#include "czt_zoom.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_PI 3.14159265358979323846

static float g_samples[SIGNAL_FRAME_SAMPLES];
static unsigned int g_failures;

#define CHECK_TRUE(condition)                                                   \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            (void)fprintf(                                                      \
                stderr,                                                         \
                "FAIL %s:%d: %s\n",                                           \
                __FILE__,                                                       \
                __LINE__,                                                       \
                #condition);                                                    \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

#define CHECK_U32(expected, actual)                                             \
    do                                                                          \
    {                                                                           \
        const uint32_t expected_value = (uint32_t)(expected);                   \
        const uint32_t actual_value = (uint32_t)(actual);                       \
        if (expected_value != actual_value)                                     \
        {                                                                       \
            (void)fprintf(                                                      \
                stderr,                                                         \
                "FAIL %s:%d: expected %lu, got %lu\n",                       \
                __FILE__,                                                       \
                __LINE__,                                                       \
                (unsigned long)expected_value,                                  \
                (unsigned long)actual_value);                                   \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

static void generate_tone(
    uint32_t sample_rate_Hz,
    double frequency_Hz,
    double amplitude_V,
    double phase_rad)
{
    size_t index;

    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        const double phase = (2.0 * TEST_PI * frequency_Hz * (double)index
            / (double)sample_rate_Hz) + phase_rad;

        g_samples[index] = (float)(amplitude_V * cos(phase));
    }
}

static void generate_two_tones(
    uint32_t sample_rate_Hz,
    double first_frequency_Hz,
    double first_amplitude_V,
    double second_frequency_Hz,
    double second_amplitude_V)
{
    size_t index;

    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        const double first_phase = 2.0 * TEST_PI * first_frequency_Hz
            * (double)index / (double)sample_rate_Hz;
        const double second_phase = 2.0 * TEST_PI * second_frequency_Hz
            * (double)index / (double)sample_rate_Hz;

        g_samples[index] = (float)(
            (first_amplitude_V * cos(first_phase + 0.31))
            + (second_amplitude_V * cos(second_phase - 0.72)));
    }
}

static float nearest_fft_frequency(
    uint32_t sample_rate_Hz,
    double frequency_Hz)
{
    const double bin_spacing_Hz = (double)sample_rate_Hz
        / (double)SIGNAL_FRAME_SAMPLES;
    const double nearest_bin = floor(
        (frequency_Hz / bin_spacing_Hz) + 0.5);

    return (float)(nearest_bin * bin_spacing_Hz);
}

static void check_single_tone_refinement(
    uint32_t sample_rate_Hz,
    double frequency_Hz,
    double tolerance_Hz)
{
    CztRefinedPeak result;
    const float coarse_frequency_Hz = nearest_fft_frequency(
        sample_rate_Hz,
        frequency_Hz);
    double error_Hz;

    generate_tone(sample_rate_Hz, frequency_Hz, 0.4, 0.31);
    result = CztZoom_Refine(
        g_samples,
        sample_rate_Hz,
        coarse_frequency_Hz);
    error_Hz = fabs((double)result.frequency_Hz - frequency_Hz);

    if ((result.valid == 0U)
        || !isfinite(result.frequency_Hz)
        || !isfinite(result.peak_power)
        || (result.peak_power <= 0.0f)
        || (error_Hz > tolerance_Hz))
    {
        (void)fprintf(
            stderr,
            "FAIL Fs=%lu tone=%.6f coarse=%.6f result=%.6f "
            "error=%.6f valid=%u power=%.9g\n",
            (unsigned long)sample_rate_Hz,
            frequency_Hz,
            (double)coarse_frequency_Hz,
            (double)result.frequency_Hz,
            error_Hz,
            (unsigned int)result.valid,
            (double)result.peak_power);
        ++g_failures;
    }
}

static void test_absolute_frequency_matrix_is_within_100_hz(void)
{
    static const uint32_t sample_rate_Hz[] = {
        2048780U,
        10000000U,
        20000000U
    };
    static const double frequency_Hz[] = {
        10000.0,
        123456.0,
        499900.0,
        500000.0
    };
    size_t rate_index;
    size_t frequency_index;

    for (rate_index = 0U;
         rate_index < (sizeof(sample_rate_Hz) / sizeof(sample_rate_Hz[0]));
         ++rate_index)
    {
        for (frequency_index = 0U;
             frequency_index < (sizeof(frequency_Hz) / sizeof(frequency_Hz[0]));
             ++frequency_index)
        {
            check_single_tone_refinement(
                sample_rate_Hz[rate_index],
                frequency_Hz[frequency_index],
                100.0);
        }
    }
}

static void check_fractional_bin_offset(
    uint32_t sample_rate_Hz,
    uint16_t coarse_bin,
    double offset_bins)
{
    const double bin_spacing_Hz = (double)sample_rate_Hz
        / (double)SIGNAL_FRAME_SAMPLES;
    const double coarse_frequency_Hz =
        (double)coarse_bin * bin_spacing_Hz;
    const double frequency_Hz =
        ((double)coarse_bin + offset_bins) * bin_spacing_Hz;
    CztRefinedPeak result;
    double error_Hz;

    generate_tone(sample_rate_Hz, frequency_Hz, 0.4, -0.47);
    result = CztZoom_Refine(
        g_samples,
        sample_rate_Hz,
        (float)coarse_frequency_Hz);
    error_Hz = fabs((double)result.frequency_Hz - frequency_Hz);

    if ((result.valid == 0U) || (error_Hz > 100.0))
    {
        (void)fprintf(
            stderr,
            "FAIL offset Fs=%lu bin=%u offset=%.3f result=%.6f "
            "expected=%.6f error=%.6f valid=%u\n",
            (unsigned long)sample_rate_Hz,
            (unsigned int)coarse_bin,
            offset_bins,
            (double)result.frequency_Hz,
            frequency_Hz,
            error_Hz,
            (unsigned int)result.valid);
        ++g_failures;
    }
}

static void test_fractional_coarse_bin_offsets_are_refined(void)
{
    check_fractional_bin_offset(2048780U, 200U, 0.1);
    check_fractional_bin_offset(10000000U, 40U, 0.5);
    check_fractional_bin_offset(20000000U, 50U, 0.9);
}

static void test_invalid_inputs_and_zero_energy_are_rejected(void)
{
    CztRefinedPeak result;
    size_t index;

    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        g_samples[index] = 0.0f;
    }

    result = CztZoom_Refine(g_samples, 2048780U, 100000.0f);
    CHECK_U32(0U, result.valid);
    CHECK_TRUE(result.frequency_Hz == 0.0f);
    CHECK_TRUE(result.peak_power == 0.0f);

    result = CztZoom_Refine(NULL, 2048780U, 100000.0f);
    CHECK_U32(0U, result.valid);

    result = CztZoom_Refine(g_samples, 0U, 100000.0f);
    CHECK_U32(0U, result.valid);

    result = CztZoom_Refine(g_samples, 2048780U, 0.0f);
    CHECK_U32(0U, result.valid);

    result = CztZoom_Refine(g_samples, 2048780U, 1024390.0f);
    CHECK_U32(0U, result.valid);

    generate_tone(2048780U, 100000.0, 0.4, 0.0);
    g_samples[17U] = NAN;
    result = CztZoom_Refine(g_samples, 2048780U, 100000.0f);
    CHECK_U32(0U, result.valid);
}

static void test_peak_on_local_interval_edge_is_rejected(void)
{
    const uint32_t sample_rate_Hz = 2048780U;
    const double bin_spacing_Hz = (double)sample_rate_Hz
        / (double)SIGNAL_FRAME_SAMPLES;
    const double frequency_Hz = 100000.0;
    const float coarse_frequency_Hz = (float)(
        frequency_Hz - (2.0 * bin_spacing_Hz));
    CztRefinedPeak result;

    generate_tone(sample_rate_Hz, frequency_Hz, 0.4, 0.0);
    result = CztZoom_Refine(
        g_samples,
        sample_rate_Hz,
        coarse_frequency_Hz);

    CHECK_U32(0U, result.valid);
}

static void test_stronger_300khz_tone_does_not_bias_100khz_refinement(void)
{
    const uint32_t sample_rate_Hz = 2048780U;
    CztRefinedPeak result;
    const float coarse_frequency_Hz = nearest_fft_frequency(
        sample_rate_Hz,
        100000.0);
    double error_Hz;

    generate_two_tones(
        sample_rate_Hz,
        100000.0,
        0.10,
        300000.0,
        0.40);
    result = CztZoom_Refine(
        g_samples,
        sample_rate_Hz,
        coarse_frequency_Hz);
    error_Hz = fabs((double)result.frequency_Hz - 100000.0);

    if ((result.valid == 0U) || (error_Hz > 250.0))
    {
        (void)fprintf(
            stderr,
            "FAIL leakage result=%.6f error=%.6f valid=%u\n",
            (double)result.frequency_Hz,
            error_Hz,
            (unsigned int)result.valid);
        ++g_failures;
    }
}

int main(void)
{
    test_absolute_frequency_matrix_is_within_100_hz();
    test_fractional_coarse_bin_offsets_are_refined();
    test_invalid_inputs_and_zero_energy_are_rejected();
    test_peak_on_local_interval_edge_is_rejected();
    test_stronger_300khz_tone_does_not_bias_100khz_refinement();

    if (g_failures != 0U)
    {
        (void)fprintf(stderr, "%u CZT test(s) failed\n", g_failures);
        return 1;
    }

    (void)puts("czt_zoom_tests: PASS");
    return 0;
}