#include "harmonic_model.h"
#include "orthogonal_fit.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_PI 3.14159265358979323846

typedef struct
{
    double frequency_Hz;
    double amplitude_peak_V;
    double phase_rad;
} TestTone;

static float g_samples[SIGNAL_FRAME_SAMPLES];
static uint8_t g_valid_mask[SIGNAL_FRAME_SAMPLES];
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
                "FAIL %s:%d: expected %lu, got %lu\n",                        \
                __FILE__,                                                       \
                __LINE__,                                                       \
                (unsigned long)expected_value,                                  \
                (unsigned long)actual_value);                                   \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

#define CHECK_NEAR(expected, actual, tolerance)                                 \
    do                                                                          \
    {                                                                           \
        const double expected_value = (double)(expected);                       \
        const double actual_value = (double)(actual);                           \
        if ((!isfinite(actual_value))                                           \
            || (fabs(actual_value - expected_value) > (double)(tolerance)))     \
        {                                                                       \
            (void)fprintf(                                                      \
                stderr,                                                         \
                "FAIL %s:%d: expected %.12g, got %.12g, tolerance %.12g\n",   \
                __FILE__,                                                       \
                __LINE__,                                                       \
                expected_value,                                                 \
                actual_value,                                                   \
                (double)(tolerance));                                           \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

static double phase_error(double actual, double expected)
{
    double difference = actual - expected;

    while (difference >= TEST_PI)
    {
        difference -= 2.0 * TEST_PI;
    }
    while (difference < -TEST_PI)
    {
        difference += 2.0 * TEST_PI;
    }
    return fabs(difference);
}

static void generate_signal(
    uint32_t sample_rate_Hz,
    double dc_V,
    const TestTone tones[],
    size_t tone_count)
{
    size_t sample_index;

    for (sample_index = 0U;
         sample_index < SIGNAL_FRAME_SAMPLES;
         ++sample_index)
    {
        double sample = dc_V;
        size_t tone_index;

        for (tone_index = 0U; tone_index < tone_count; ++tone_index)
        {
            const double angle =
                (2.0 * TEST_PI * tones[tone_index].frequency_Hz
                    * (double)sample_index / (double)sample_rate_Hz)
                + tones[tone_index].phase_rad;

            sample += tones[tone_index].amplitude_peak_V * cos(angle);
        }
        g_samples[sample_index] = (float)sample;
    }
}

static void apply_lower_clip(double threshold_V)
{
    size_t sample_index;

    for (sample_index = 0U;
         sample_index < SIGNAL_FRAME_SAMPLES;
         ++sample_index)
    {
        if ((double)g_samples[sample_index] <= threshold_V)
        {
            g_samples[sample_index] = (float)threshold_V;
            g_valid_mask[sample_index] = 0U;
        }
        else
        {
            g_valid_mask[sample_index] = 1U;
        }
    }
}

static double independent_iq_amplitude(
    uint32_t sample_rate_Hz,
    double frequency_Hz)
{
    double cosine_sum = 0.0;
    double sine_sum = 0.0;
    size_t sample_index;

    for (sample_index = 0U;
         sample_index < SIGNAL_FRAME_SAMPLES;
         ++sample_index)
    {
        const double angle = 2.0 * TEST_PI * frequency_Hz
            * (double)sample_index / (double)sample_rate_Hz;
        const double sample = (double)g_samples[sample_index];

        cosine_sum += sample * cos(angle);
        sine_sum += sample * sin(angle);
    }

    cosine_sum *= 2.0 / (double)SIGNAL_FRAME_SAMPLES;
    sine_sum *= 2.0 / (double)SIGNAL_FRAME_SAMPLES;
    return hypot(cosine_sum, sine_sum);
}

static void set_peak(
    CztRefinedPeak *peak,
    float frequency_Hz,
    float peak_power)
{
    peak->frequency_Hz = frequency_Hz;
    peak->peak_power = peak_power;
    peak->valid = 1U;
}

static void check_fitted_tone(
    const SignalComponent *actual,
    const TestTone *expected)
{
    CHECK_NEAR(expected->frequency_Hz, actual->frequency_Hz, 0.01);
    CHECK_NEAR(
        expected->amplitude_peak_V,
        actual->amplitude_peak_V,
        0.002);
    CHECK_TRUE((double)actual->phase_rad >= -TEST_PI);
    CHECK_TRUE((double)actual->phase_rad < TEST_PI);
    CHECK_TRUE(phase_error(
        (double)actual->phase_rad,
        expected->phase_rad) <= 0.02);
}

static void test_joint_fit_removes_noninteger_cycle_crosstalk(void)
{
    static const TestTone tones[] = {
        {100123.0, 0.35, 0.31},
        {101456.0, 0.22, -0.72}
    };
    const uint32_t sample_rate_Hz = 2048780U;
    HarmonicModel model = {
        2U,
        {100123.0f, 101456.0f, 0.0f, 0.0f},
        {1U, 1U, 0U, 0U}
    };
    OrthogonalFitResult fit;
    double first_independent_error;
    double second_independent_error;

    generate_signal(
        sample_rate_Hz,
        0.0,
        tones,
        sizeof(tones) / sizeof(tones[0]));
    first_independent_error = fabs(
        independent_iq_amplitude(sample_rate_Hz, tones[0].frequency_Hz)
        - tones[0].amplitude_peak_V);
    second_independent_error = fabs(
        independent_iq_amplitude(sample_rate_Hz, tones[1].frequency_Hz)
        - tones[1].amplitude_peak_V);

    CHECK_TRUE(first_independent_error > 0.005);
    CHECK_TRUE(second_independent_error > 0.005);
    CHECK_TRUE(OrthogonalFit_Run(
        g_samples,
        sample_rate_Hz,
        &model,
        &fit));
    CHECK_NEAR(0.0, fit.dc_V, 0.0005);
    check_fitted_tone(&fit.tone[0], &tones[0]);
    check_fitted_tone(&fit.tone[1], &tones[1]);
    CHECK_TRUE(fit.residual_rms_V < 0.00001);
    CHECK_TRUE(fit.snr_dB > 80.0);
    CHECK_TRUE(fit.pivot_ratio >= 1.0e-10);
}

static void test_model_selects_fundamental_even_when_other_peaks_are_stronger(void)
{
    CztRefinedPeak peaks[SIGNAL_MAX_CANDIDATES] = {{0}};
    HarmonicModel model;

    set_peak(&peaks[0], 350000.0f, 0.81f);
    set_peak(&peaks[1], 200000.0f, 0.49f);
    set_peak(&peaks[2], 100000.0f, 0.09f);

    CHECK_TRUE(HarmonicModel_Select(peaks, 3U, &model));
    CHECK_U32(2U, model.tone_count);
    CHECK_NEAR(100000.0, model.frequency_Hz[0], 0.01);
    CHECK_NEAR(200000.0, model.frequency_Hz[1], 0.01);
    CHECK_U32(1U, model.desired[0]);
    CHECK_U32(1U, model.desired[1]);
}

static void test_model_selects_f0_plus_third_and_fourth_harmonics(void)
{
    CztRefinedPeak peaks[SIGNAL_MAX_CANDIDATES] = {{0}};
    HarmonicModel model;

    set_peak(&peaks[0], 240000.0f, 0.64f);
    set_peak(&peaks[1], 80000.0f, 0.04f);
    set_peak(&peaks[2], 320000.0f, 0.16f);
    set_peak(&peaks[3], 175000.0f, 0.81f);

    CHECK_TRUE(HarmonicModel_Select(peaks, 4U, &model));
    CHECK_U32(3U, model.tone_count);
    CHECK_NEAR(80000.0, model.frequency_Hz[0], 0.01);
    CHECK_NEAR(240000.0, model.frequency_Hz[1], 0.01);
    CHECK_NEAR(320000.0, model.frequency_Hz[2], 0.01);
    CHECK_U32(1U, model.desired[0]);
    CHECK_U32(1U, model.desired[1]);
    CHECK_U32(1U, model.desired[2]);
}

static void test_interference_is_retained_as_nondesired_fit_column(void)
{
    static const TestTone tones[] = {
        {100000.0, 0.30, 0.10},
        {200000.0, 0.12, -0.40},
        {1100000.0, 0.20, 3.50}
    };
    CztRefinedPeak peaks[SIGNAL_MAX_CANDIDATES] = {{0}};
    HarmonicModel model;
    OrthogonalFitResult fit;

    set_peak(&peaks[0], 1100000.0f, 0.04f);
    set_peak(&peaks[1], 200000.0f, 0.0144f);
    set_peak(&peaks[2], 100000.0f, 0.09f);

    CHECK_TRUE(HarmonicModel_Select(peaks, 3U, &model));
    CHECK_U32(3U, model.tone_count);
    CHECK_NEAR(100000.0, model.frequency_Hz[0], 0.01);
    CHECK_NEAR(200000.0, model.frequency_Hz[1], 0.01);
    CHECK_NEAR(1100000.0, model.frequency_Hz[2], 0.01);
    CHECK_U32(1U, model.desired[0]);
    CHECK_U32(1U, model.desired[1]);
    CHECK_U32(0U, model.desired[2]);

    generate_signal(
        10000000U,
        0.08,
        tones,
        sizeof(tones) / sizeof(tones[0]));
    CHECK_TRUE(OrthogonalFit_Run(
        g_samples,
        10000000U,
        &model,
        &fit));
    CHECK_NEAR(0.08, fit.dc_V, 0.0005);
    check_fitted_tone(&fit.tone[0], &tones[0]);
    check_fitted_tone(&fit.tone[1], &tones[1]);
    check_fitted_tone(&fit.tone[2], &tones[2]);
}

static void test_close_frequency_columns_are_rejected_as_ill_conditioned(void)
{
    static const TestTone tone = {10000.0, 0.30, 0.20};
    HarmonicModel model = {
        2U,
        {10000.0f, 10000.0009765625f, 0.0f, 0.0f},
        {1U, 1U, 0U, 0U}
    };
    OrthogonalFitResult fit;

    CHECK_TRUE(model.frequency_Hz[0] != model.frequency_Hz[1]);
    generate_signal(2048780U, 0.0, &tone, 1U);
    CHECK_TRUE(!OrthogonalFit_Run(
        g_samples,
        2048780U,
        &model,
        &fit));
    CHECK_TRUE(fit.pivot_ratio < 1.0e-10);
}

static void test_masked_fit_recovers_lower_clipped_sine(void)
{
    static const TestTone tone = {100123.0, 0.125, 0.31};
    HarmonicModel model = {
        1U,
        {100123.0f, 0.0f, 0.0f, 0.0f},
        {1U, 0U, 0U, 0U}
    };
    OrthogonalFitResult fit;

    generate_signal(2048780U, 0.020, &tone, 1U);
    apply_lower_clip(-0.030);

    CHECK_TRUE(OrthogonalFit_RunMasked(
        g_samples,
        g_valid_mask,
        -0.030,
        2048780U,
        &model,
        &fit));
    CHECK_NEAR(0.020, fit.dc_V, 0.001);
    check_fitted_tone(&fit.tone[0], &tone);
    CHECK_TRUE(fit.fitted_sample_count >= SIGNAL_FRAME_SAMPLES / 4U);
    CHECK_U32(0U, fit.clip_violation_count);
    CHECK_TRUE(fit.clip_violation_rms_V <= 0.001);
}

static void test_masked_fit_exposes_wrong_model(void)
{
    static const TestTone tone = {100123.0, 0.125, 0.31};
    HarmonicModel wrong_model = {
        1U,
        {103500.0f, 0.0f, 0.0f, 0.0f},
        {1U, 0U, 0U, 0U}
    };
    OrthogonalFitResult fit;

    generate_signal(2048780U, 0.020, &tone, 1U);
    apply_lower_clip(-0.030);

    CHECK_TRUE(OrthogonalFit_RunMasked(
        g_samples,
        g_valid_mask,
        -0.030,
        2048780U,
        &wrong_model,
        &fit));
    CHECK_TRUE((fit.clip_violation_count != 0U)
        || (fit.residual_rms_V > 0.020));
}

static void test_masked_fit_rejects_too_few_samples(void)
{
    static const TestTone tone = {100123.0, 0.125, 0.31};
    HarmonicModel model = {
        1U,
        {100123.0f, 0.0f, 0.0f, 0.0f},
        {1U, 0U, 0U, 0U}
    };
    OrthogonalFitResult fit;
    size_t sample_index;

    generate_signal(2048780U, 0.020, &tone, 1U);
    for (sample_index = 0U;
         sample_index < SIGNAL_FRAME_SAMPLES;
         ++sample_index)
    {
        g_valid_mask[sample_index] = (sample_index < 8U) ? 1U : 0U;
    }

    CHECK_TRUE(!OrthogonalFit_RunMasked(
        g_samples,
        g_valid_mask,
        -0.030,
        2048780U,
        &model,
        &fit));
}

static void test_invalid_inputs_are_rejected(void)
{
    CztRefinedPeak peaks[SIGNAL_MAX_CANDIDATES] = {{0}};
    HarmonicModel model = {
        1U,
        {100000.0f, 0.0f, 0.0f, 0.0f},
        {1U, 0U, 0U, 0U}
    };
    OrthogonalFitResult fit;

    CHECK_TRUE(!HarmonicModel_Select(NULL, 1U, &model));
    CHECK_TRUE(!HarmonicModel_Select(peaks, 0U, &model));
    CHECK_TRUE(!HarmonicModel_Select(
        peaks,
        SIGNAL_MAX_CANDIDATES + 1U,
        &model));
    CHECK_TRUE(!HarmonicModel_Select(peaks, 1U, NULL));

    set_peak(&peaks[0], NAN, 1.0f);
    CHECK_TRUE(!HarmonicModel_Select(peaks, 1U, &model));

    generate_signal(2048780U, 0.0, NULL, 0U);
    CHECK_TRUE(!OrthogonalFit_Run(NULL, 2048780U, &model, &fit));
    CHECK_TRUE(!OrthogonalFit_Run(g_samples, 0U, &model, &fit));
    CHECK_TRUE(!OrthogonalFit_Run(g_samples, 2048780U, NULL, &fit));
    CHECK_TRUE(!OrthogonalFit_Run(
        g_samples,
        2048780U,
        &model,
        NULL));

    model.tone_count = 0U;
    CHECK_TRUE(!OrthogonalFit_Run(
        g_samples,
        2048780U,
        &model,
        &fit));
    model.tone_count = SIGNAL_MAX_FIT_TONES + 1U;
    CHECK_TRUE(!OrthogonalFit_Run(
        g_samples,
        2048780U,
        &model,
        &fit));

    model.tone_count = 1U;
    model.frequency_Hz[0] = 1024390.0f;
    CHECK_TRUE(!OrthogonalFit_Run(
        g_samples,
        2048780U,
        &model,
        &fit));
    model.frequency_Hz[0] = 100000.0f;
    model.desired[0] = 2U;
    CHECK_TRUE(!OrthogonalFit_Run(
        g_samples,
        2048780U,
        &model,
        &fit));

    model.desired[0] = 1U;
    g_samples[17U] = NAN;
    CHECK_TRUE(!OrthogonalFit_Run(
        g_samples,
        2048780U,
        &model,
        &fit));
}

int main(void)
{
    test_joint_fit_removes_noninteger_cycle_crosstalk();
    test_model_selects_fundamental_even_when_other_peaks_are_stronger();
    test_model_selects_f0_plus_third_and_fourth_harmonics();
    test_interference_is_retained_as_nondesired_fit_column();
    test_close_frequency_columns_are_rejected_as_ill_conditioned();
    test_masked_fit_recovers_lower_clipped_sine();
    test_masked_fit_exposes_wrong_model();
    test_masked_fit_rejects_too_few_samples();
    test_invalid_inputs_are_rejected();

    if (g_failures != 0U)
    {
        (void)fprintf(
            stderr,
            "%u orthogonal fit test(s) failed\n",
            g_failures);
        return 1;
    }

    (void)puts("orthogonal_fit_tests: PASS");
    return 0;
}
