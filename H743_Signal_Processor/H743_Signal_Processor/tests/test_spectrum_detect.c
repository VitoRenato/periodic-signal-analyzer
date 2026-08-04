#include "spectrum_detect.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#define TEST_SAMPLE_RATE_HZ 2048780U
#define TEST_PI 3.14159265358979323846f
#define TEST_MINIMUM_FREQUENCY_HZ 10000.0f
#define TEST_MAXIMUM_FREQUENCY_HZ 500000.0f

static float g_samples[SIGNAL_FRAME_SAMPLES];
static SpectrumCandidate g_candidates[SIGNAL_MAX_CANDIDATES];
static unsigned int g_failures;

#define CHECK_SIZE(expected, actual)                                           \
    do                                                                          \
    {                                                                           \
        const size_t expected_value = (size_t)(expected);                       \
        const size_t actual_value = (size_t)(actual);                           \
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

static void generate_two_tone(
    float dc_V,
    float first_amplitude_V,
    float first_frequency_Hz,
    float first_phase_rad,
    float second_amplitude_V,
    float second_frequency_Hz,
    float second_phase_rad)
{
    size_t index;

    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        const float time_samples = (float)index / (float)TEST_SAMPLE_RATE_HZ;

        g_samples[index] = dc_V
            + first_amplitude_V
                * cosf(
                    (2.0f * TEST_PI * first_frequency_Hz * time_samples)
                    + first_phase_rad)
            + second_amplitude_V
                * cosf(
                    (2.0f * TEST_PI * second_frequency_Hz * time_samples)
                    + second_phase_rad);
    }
}

static void generate_ten_bin_centered_tones(void)
{
    static const uint16_t tone_bin[10] = {
        50U, 100U, 150U, 200U, 250U,
        300U, 350U, 400U, 450U, 500U
    };
    static const float amplitude_V[10] = {
        0.10f, 0.45f, 0.15f, 0.40f, 0.20f,
        0.35f, 0.25f, 0.30f, 0.05f, 0.50f
    };
    size_t sample_index;

    for (sample_index = 0U;
         sample_index < SIGNAL_FRAME_SAMPLES;
         ++sample_index)
    {
        double sample = 0.0;
        size_t tone_index;

        for (tone_index = 0U; tone_index < 10U; ++tone_index)
        {
            const double phase = 2.0 * (double)TEST_PI
                * (double)tone_bin[tone_index] * (double)sample_index
                / (double)SIGNAL_FRAME_SAMPLES;

            sample += (double)amplitude_V[tone_index] * cos(phase);
        }
        g_samples[sample_index] = (float)sample;
    }
}

static size_t find_candidates(void)
{
    return SpectrumDetect_Find(
        g_samples,
        TEST_SAMPLE_RATE_HZ,
        TEST_MINIMUM_FREQUENCY_HZ,
        TEST_MAXIMUM_FREQUENCY_HZ,
        g_candidates);
}

static int contains_frequency(
    size_t candidate_count,
    float expected_frequency_Hz)
{
    const float tolerance_Hz =
        (float)TEST_SAMPLE_RATE_HZ / (float)SIGNAL_FRAME_SAMPLES;
    size_t index;

    for (index = 0U; index < candidate_count; ++index)
    {
        if (fabsf(
                g_candidates[index].coarse_frequency_Hz
                - expected_frequency_Hz) <= tolerance_Hz)
        {
            return 1;
        }
    }

    return 0;
}

static void test_single_tone_returns_one_coarse_candidate(void)
{
    const size_t candidate_count = (
        generate_two_tone(0.0f, 0.400f, 123456.0f, 0.31f, 0.0f, 0.0f, 0.0f),
        find_candidates());

    CHECK_SIZE(1U, candidate_count);
    CHECK_TRUE(contains_frequency(candidate_count, 123456.0f));
}

static void test_two_tones_are_both_retained(void)
{
    const size_t candidate_count = (
        generate_two_tone(
            0.125f,
            0.400f,
            123456.0f,
            0.31f,
            0.150f,
            246912.0f,
            -0.72f),
        find_candidates());

    CHECK_TRUE(candidate_count >= 2U);
    CHECK_TRUE(contains_frequency(candidate_count, 123456.0f));
    CHECK_TRUE(contains_frequency(candidate_count, 246912.0f));
}

static void test_stronger_second_harmonic_does_not_delete_fundamental(void)
{
    const size_t candidate_count = (
        generate_two_tone(
            0.0f,
            0.150f,
            123456.0f,
            0.31f,
            0.400f,
            246912.0f,
            -0.72f),
        find_candidates());

    CHECK_TRUE(contains_frequency(candidate_count, 123456.0f));
    CHECK_TRUE(contains_frequency(candidate_count, 246912.0f));
}

static void test_only_eight_strongest_candidates_are_sorted_by_frequency(void)
{
    static const uint16_t expected_bin[SIGNAL_MAX_CANDIDATES] = {
        100U, 150U, 200U, 250U, 300U, 350U, 400U, 500U
    };
    size_t candidate_count;
    size_t index;

    generate_ten_bin_centered_tones();
    candidate_count = find_candidates();

    CHECK_SIZE(SIGNAL_MAX_CANDIDATES, candidate_count);
    for (index = 0U; index < candidate_count; ++index)
    {
        CHECK_SIZE(expected_bin[index], g_candidates[index].bin);
    }
}

static void test_dc_only_frame_returns_no_candidates(void)
{
    const size_t candidate_count = (
        generate_two_tone(0.125f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
        find_candidates());

    CHECK_SIZE(0U, candidate_count);
}

static void test_tone_outside_search_band_is_excluded(void)
{
    const size_t candidate_count = (
        generate_two_tone(0.0f, 0.400f, 501000.0f, 0.31f, 0.0f, 0.0f, 0.0f),
        find_candidates());


    CHECK_SIZE(0U, candidate_count);
}

int main(void)
{
    test_single_tone_returns_one_coarse_candidate();
    test_two_tones_are_both_retained();
    test_stronger_second_harmonic_does_not_delete_fundamental();
    test_only_eight_strongest_candidates_are_sorted_by_frequency();
    test_dc_only_frame_returns_no_candidates();
    test_tone_outside_search_band_is_excluded();

    if (g_failures != 0U)
    {
        (void)fprintf(
            stderr,
            "%u spectrum detector test(s) failed\n",
            g_failures);
        return 1;
    }

    (void)puts("spectrum_detect_tests: PASS");
    return 0;
}