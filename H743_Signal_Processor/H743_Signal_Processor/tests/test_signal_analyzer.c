#include "signal_analyzer.h"
#include "ad9226_calibration.h"
#include "signal_preprocess.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define TEST_ADC_V_PER_CODE   ((double)AD9226_ADC_V_PER_CODE)
#define TEST_FRONTEND_GAIN    2.0
#define TEST_BNC_V_PER_CODE   (TEST_ADC_V_PER_CODE / TEST_FRONTEND_GAIN)

static uint16_t g_samples[SIGNAL_FRAME_SAMPLES];
static float g_fit_samples[SIGNAL_FRAME_SAMPLES];
static float g_spectrum_samples[SIGNAL_FRAME_SAMPLES];
static uint8_t g_valid_mask[SIGNAL_FRAME_SAMPLES];
static unsigned int g_failures;

#define CHECK_U32(expected, actual)                                             \
    do                                                                          \
    {                                                                           \
        const uint32_t expected_value = (uint32_t)(expected);                   \
        const uint32_t actual_value = (uint32_t)(actual);                       \
        if (expected_value != actual_value)                                     \
        {                                                                       \
            (void)fprintf(                                                      \
                stderr,                                                         \
                "FAIL %s:%d: expected %lu, got %lu\n",                        \
                __FILE__,                                                       \
                __LINE__,                                                       \
                (unsigned long)expected_value,                                  \
                (unsigned long)actual_value);                                   \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

#define CHECK_NE_U32(unexpected, actual)                                        \
    do                                                                          \
    {                                                                           \
        const uint32_t unexpected_value = (uint32_t)(unexpected);               \
        const uint32_t actual_value = (uint32_t)(actual);                       \
        if (unexpected_value == actual_value)                                   \
        {                                                                       \
            (void)fprintf(                                                      \
                stderr,                                                         \
                "FAIL %s:%d: value unexpectedly equals %lu\n",                \
                __FILE__,                                                       \
                __LINE__,                                                       \
                (unsigned long)unexpected_value);                               \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

#define CHECK_DOUBLE_NEAR(expected, actual, tolerance)                          \
    do                                                                          \
    {                                                                           \
        const double expected_value = (double)(expected);                       \
        const double actual_value = (double)(actual);                           \
        double difference = actual_value - expected_value;                     \
        if (difference < 0.0)                                                   \
        {                                                                       \
            difference = -difference;                                           \
        }                                                                       \
        if (difference > (double)(tolerance))                                   \
        {                                                                       \
            (void)fprintf(                                                      \
                stderr,                                                         \
                "FAIL %s:%d: expected %.12g, got %.12g\n",                    \
                __FILE__,                                                       \
                __LINE__,                                                       \
                expected_value,                                                 \
                actual_value);                                                  \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

static void fill_samples(uint16_t value)
{
    size_t index;

    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        g_samples[index] = value;
    }
}

static void test_null_samples_are_rejected(void)
{
    SignalResult result;

    CHECK_U32(
        SIGNAL_STATUS_BAD_ARGUMENT,
        SignalAnalyzer_Process(NULL, 2048780U, &result));
    CHECK_U32(SIGNAL_STATUS_BAD_ARGUMENT, result.status);
    CHECK_U32(2048780U, result.sample_rate_Hz);
}

static void test_null_result_is_rejected(void)
{
    fill_samples(2055U);

    CHECK_U32(
        SIGNAL_STATUS_BAD_ARGUMENT,
        SignalAnalyzer_Process(g_samples, 2048780U, NULL));
}

static void test_sample_rate_outside_supported_range_is_rejected(void)
{
    SignalResult result;

    fill_samples(2055U);

    CHECK_U32(
        SIGNAL_STATUS_BAD_ARGUMENT,
        SignalAnalyzer_Process(g_samples, 4999U, &result));
    CHECK_U32(SIGNAL_STATUS_BAD_ARGUMENT, result.status);
    CHECK_U32(4999U, result.sample_rate_Hz);

    CHECK_U32(
        SIGNAL_STATUS_BAD_ARGUMENT,
        SignalAnalyzer_Process(g_samples, 21000001U, &result));
    CHECK_U32(SIGNAL_STATUS_BAD_ARGUMENT, result.status);
    CHECK_U32(21000001U, result.sample_rate_Hz);
}

static void test_static_zero_code_frame_has_no_signal(void)
{
    SignalResult result;

    fill_samples(2055U);

    CHECK_U32(
        SIGNAL_STATUS_NO_SIGNAL,
        SignalAnalyzer_Process(g_samples, 2048780U, &result));
    CHECK_U32(SIGNAL_STATUS_NO_SIGNAL, result.status);
    CHECK_U32(2048780U, result.sample_rate_Hz);
    CHECK_U32(0U, result.component_count);
}

static void test_more_than_one_percent_rail_codes_are_clipped(void)
{
    SignalResult result;
    size_t index;

    fill_samples(2055U);
    for (index = 0U; index < 41U; ++index)
    {
        g_samples[index] = 0U;
    }

    CHECK_U32(
        SIGNAL_STATUS_CLIPPED,
        SignalAnalyzer_Process(g_samples, 2048780U, &result));
    CHECK_U32(SIGNAL_STATUS_CLIPPED, result.status);
}

static void test_nonzero_bits_above_bit_eleven_are_rejected(void)
{
    SignalResult result;

    fill_samples(2055U);
    g_samples[17] = 0x1807U;

    CHECK_U32(
        SIGNAL_STATUS_CLIPPED,
        SignalAnalyzer_Process(g_samples, 2048780U, &result));
    CHECK_U32(SIGNAL_STATUS_CLIPPED, result.status);
}

static void test_clipping_takes_priority_over_zero_ac_energy(void)
{
    SignalResult result;

    fill_samples(0U);

    CHECK_U32(
        SIGNAL_STATUS_CLIPPED,
        SignalAnalyzer_Process(g_samples, 2048780U, &result));
    CHECK_U32(SIGNAL_STATUS_CLIPPED, result.status);
}

static void test_supported_sample_rate_boundaries_are_accepted(void)
{
    SignalResult result;

    fill_samples(2055U);

    CHECK_U32(
        SIGNAL_STATUS_NO_SIGNAL,
        SignalAnalyzer_Process(g_samples, 5000U, &result));
    CHECK_U32(5000U, result.sample_rate_Hz);

    CHECK_U32(
        SIGNAL_STATUS_NO_SIGNAL,
        SignalAnalyzer_Process(g_samples, 21000000U, &result));
    CHECK_U32(21000000U, result.sample_rate_Hz);
}

static void test_forty_rail_codes_do_not_cross_one_percent_limit(void)
{
    SignalResult result;
    size_t index;

    fill_samples(2055U);
    for (index = 0U; index < 40U; ++index)
    {
        g_samples[index] = 0U;
    }

    CHECK_NE_U32(
        SIGNAL_STATUS_CLIPPED,
        SignalAnalyzer_Process(g_samples, 2048780U, &result));
}

static void test_bad_argument_clears_previous_result(void)
{
    SignalResult result;

    (void)memset(&result, 0xA5, sizeof(result));

    CHECK_U32(
        SIGNAL_STATUS_BAD_ARGUMENT,
        SignalAnalyzer_Process(NULL, 2048780U, &result));
    CHECK_U32(SIGNAL_STATUS_BAD_ARGUMENT, result.status);
    CHECK_U32(2048780U, result.sample_rate_Hz);
    CHECK_U32(0U, result.component_count);
    CHECK_U32(0U, result.recommended_sample_rate_Hz);
    CHECK_DOUBLE_NEAR(0.0, result.component[2].phase_rad, 0.0);
}

static void test_preprocess_calibration_and_dc_removal(void)
{
    SignalPreprocessStats stats;

    fill_samples(2055U);
    g_samples[0] = 2056U;
    g_samples[1] = 2054U;

    SignalPreprocess_Run(
        g_samples,
        g_fit_samples,
        g_spectrum_samples,
        g_valid_mask,
        &stats);

    CHECK_U32(0U, stats.rail_code_count);
    CHECK_U32(0U, stats.high_bit_count);
    CHECK_DOUBLE_NEAR(0.0, stats.dc_mean_V, 1.0e-12);
    CHECK_DOUBLE_NEAR(
        TEST_BNC_V_PER_CODE,
        g_spectrum_samples[0],
        1.0e-8);
    CHECK_DOUBLE_NEAR(
        -TEST_BNC_V_PER_CODE,
        g_spectrum_samples[1],
        1.0e-8);
    CHECK_DOUBLE_NEAR(0.0, g_spectrum_samples[2], 1.0e-12);
    CHECK_DOUBLE_NEAR(
        TEST_BNC_V_PER_CODE,
        g_fit_samples[0],
        1.0e-8);
    CHECK_DOUBLE_NEAR(
        -TEST_BNC_V_PER_CODE,
        g_fit_samples[1],
        1.0e-8);
    CHECK_DOUBLE_NEAR(
        2.0 * TEST_BNC_V_PER_CODE * TEST_BNC_V_PER_CODE,
        stats.ac_energy_V2,
        1.0e-10);
}

static void test_preprocess_removes_constant_offset(void)
{
    SignalPreprocessStats stats;

    fill_samples(2100U);

    SignalPreprocess_Run(
        g_samples,
        g_fit_samples,
        g_spectrum_samples,
        g_valid_mask,
        &stats);

    CHECK_DOUBLE_NEAR(
        45.0 * TEST_BNC_V_PER_CODE,
        stats.dc_mean_V,
        1.0e-7);
    CHECK_DOUBLE_NEAR(
        45.0 * TEST_BNC_V_PER_CODE,
        g_fit_samples[0],
        1.0e-7);
    CHECK_DOUBLE_NEAR(0.0, g_spectrum_samples[0], 1.0e-12);
    CHECK_DOUBLE_NEAR(0.0, stats.ac_energy_V2, 1.0e-12);
}

static void test_clean_frame_keeps_every_sample_valid(void)
{
    SignalPreprocessStats stats;
    size_t index;

    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        g_samples[index] = (uint16_t)(2061U + (index & 7U));
    }

    SignalPreprocess_Run(
        g_samples,
        g_fit_samples,
        g_spectrum_samples,
        g_valid_mask,
        &stats);

    CHECK_U32(0U, stats.lower_clip_detected);
    CHECK_U32(0U, stats.lower_clip_count);
    CHECK_U32(SIGNAL_FRAME_SAMPLES, stats.valid_sample_count);
    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        CHECK_U32(1U, g_valid_mask[index]);
    }
}

static void test_lower_plateau_marks_unsafe_samples_invalid(void)
{
    SignalPreprocessStats stats;
    size_t index;

    fill_samples(2100U);
    for (index = 0U; index < 512U; ++index)
    {
        g_samples[index] = (uint16_t)(2048U + (index % 3U));
    }

    SignalPreprocess_Run(
        g_samples,
        g_fit_samples,
        g_spectrum_samples,
        g_valid_mask,
        &stats);

    CHECK_U32(1U, stats.lower_clip_detected);
    CHECK_U32(512U, stats.lower_clip_count);
    CHECK_U32(512U, stats.longest_clip_run);
    CHECK_U32(SIGNAL_FRAME_SAMPLES - 512U, stats.valid_sample_count);
    CHECK_U32(0U, g_valid_mask[0U]);
    CHECK_U32(0U, g_valid_mask[511U]);
    CHECK_U32(1U, g_valid_mask[512U]);
    CHECK_DOUBLE_NEAR(
        -5.0 * TEST_BNC_V_PER_CODE,
        stats.lower_clip_threshold_V,
        1.0e-8);
}

static void test_sparse_floor_codes_do_not_create_false_clip(void)
{
    SignalPreprocessStats stats;
    size_t index;

    fill_samples(2100U);
    for (index = 0U; index < 8U; ++index)
    {
        g_samples[index * 500U] = 2048U;
    }

    SignalPreprocess_Run(
        g_samples,
        g_fit_samples,
        g_spectrum_samples,
        g_valid_mask,
        &stats);

    CHECK_U32(0U, stats.lower_clip_detected);
    CHECK_U32(8U, stats.lower_clip_count);
    CHECK_U32(1U, stats.longest_clip_run);
    CHECK_U32(SIGNAL_FRAME_SAMPLES, stats.valid_sample_count);
    for (index = 0U; index < SIGNAL_FRAME_SAMPLES; ++index)
    {
        CHECK_U32(1U, g_valid_mask[index]);
    }
}

static void test_codes_below_platform_disprove_lower_clipping(void)
{
    SignalPreprocessStats stats;
    size_t index;

    fill_samples(2100U);
    for (index = 0U; index < 100U; ++index)
    {
        g_samples[index] = 2048U;
    }
    g_samples[100U] = 2047U;

    SignalPreprocess_Run(
        g_samples,
        g_fit_samples,
        g_spectrum_samples,
        g_valid_mask,
        &stats);

    CHECK_U32(1U, stats.below_lower_clip_count);
    CHECK_U32(0U, stats.lower_clip_detected);
    CHECK_U32(SIGNAL_FRAME_SAMPLES, stats.valid_sample_count);
}

int main(void)
{
    test_null_samples_are_rejected();
    test_null_result_is_rejected();
    test_sample_rate_outside_supported_range_is_rejected();
    test_static_zero_code_frame_has_no_signal();
    test_more_than_one_percent_rail_codes_are_clipped();
    test_nonzero_bits_above_bit_eleven_are_rejected();
    test_clipping_takes_priority_over_zero_ac_energy();
    test_supported_sample_rate_boundaries_are_accepted();
    test_forty_rail_codes_do_not_cross_one_percent_limit();
    test_bad_argument_clears_previous_result();
    test_preprocess_calibration_and_dc_removal();
    test_preprocess_removes_constant_offset();
    test_clean_frame_keeps_every_sample_valid();
    test_lower_plateau_marks_unsafe_samples_invalid();
    test_sparse_floor_codes_do_not_create_false_clip();
    test_codes_below_platform_disprove_lower_clipping();

    if (g_failures != 0U)
    {
        (void)fprintf(
            stderr,
            "%u signal analyzer test(s) failed\n",
            g_failures);
        return 1;
    }

    (void)puts("signal_analyzer_tests: PASS");
    return 0;
}
