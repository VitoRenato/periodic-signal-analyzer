#include "signal_analyzer.h"
#include "ad9226_calibration.h"
#include "signal_metrics.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_PI 3.14159265358979323846
#define TEST_AD9226_ZERO_CODE 2055L
#define TEST_AD9226_ADC_V_PER_CODE ((double)AD9226_ADC_V_PER_CODE)
#define TEST_FRONTEND_AC_GAIN 2.0
#define TEST_CLIPPED_FIXTURE_SCALE \
    (TEST_AD9226_ADC_V_PER_CODE / 0.005236)

typedef struct
{
    double frequency_Hz;
    double amplitude_peak_V;
    double phase_rad;
    uint8_t desired;
} TestTone;

static uint16_t g_samples[SIGNAL_FRAME_SAMPLES];
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
        const double difference = fabs(actual_value - expected_value);          \
        if ((!isfinite(actual_value))                                           \
            || (difference > (double)(tolerance)))                              \
        {                                                                       \
            (void)fprintf(                                                      \
                stderr,                                                         \
                "FAIL %s:%d: expected %.12g, got %.12g, error %.12g\n",       \
                __FILE__,                                                       \
                __LINE__,                                                       \
                expected_value,                                                 \
                actual_value,                                                   \
                difference);                                                    \
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

static void generate_quantized_codes(
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
        double adc_voltage_V = dc_V;
        size_t tone_index;
        long code;

        for (tone_index = 0U; tone_index < tone_count; ++tone_index)
        {
            const double angle = 2.0 * TEST_PI
                * tones[tone_index].frequency_Hz
                * (double)sample_index / (double)sample_rate_Hz
                + tones[tone_index].phase_rad;

            adc_voltage_V += TEST_FRONTEND_AC_GAIN
                * tones[tone_index].amplitude_peak_V * cos(angle);
        }

        code = lround(adc_voltage_V / TEST_AD9226_ADC_V_PER_CODE)
            + TEST_AD9226_ZERO_CODE;
        if (code < 0L)
        {
            code = 0L;
        }
        else if (code > 4095L)
        {
            code = 4095L;
        }
        g_samples[sample_index] = (uint16_t)code;
    }
}

static void clamp_codes_to_lower_platform(void)
{
    size_t sample_index;

    for (sample_index = 0U;
         sample_index < SIGNAL_FRAME_SAMPLES;
         ++sample_index)
    {
        if (g_samples[sample_index] < 2048U)
        {
            g_samples[sample_index] = 2048U;
        }
    }
}

static void calculate_expected_metrics(
    const TestTone tones[],
    size_t tone_count,
    double *upp_V,
    double *urms_V)
{
    double minimum_V = 1.0e30;
    double maximum_V = -1.0e30;
    double rms_power_V2 = 0.0;
    size_t phase_index;
    size_t tone_index;

    for (tone_index = 0U; tone_index < tone_count; ++tone_index)
    {
        if (tones[tone_index].desired != 0U)
        {
            rms_power_V2 += 0.5
                * tones[tone_index].amplitude_peak_V
                * tones[tone_index].amplitude_peak_V;
        }
    }

    for (phase_index = 0U; phase_index < 65536U; ++phase_index)
    {
        const double base_phase = 2.0 * TEST_PI
            * (double)phase_index / 65536.0;
        const double fundamental_Hz = tones[0U].frequency_Hz;
        double sample_V = 0.0;

        for (tone_index = 0U; tone_index < tone_count; ++tone_index)
        {
            if (tones[tone_index].desired != 0U)
            {
                const double order = tones[tone_index].frequency_Hz
                    / fundamental_Hz;
                sample_V += tones[tone_index].amplitude_peak_V
                    * cos((order * base_phase)
                        + tones[tone_index].phase_rad);
            }
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
}

static size_t desired_tone_count(const TestTone tones[], size_t tone_count)
{
    size_t desired_count = 0U;
    size_t tone_index;

    for (tone_index = 0U; tone_index < tone_count; ++tone_index)
    {
        if (tones[tone_index].desired != 0U)
        {
            ++desired_count;
        }
    }
    return desired_count;
}

static void check_end_to_end_case(
    const char *name,
    uint32_t sample_rate_Hz,
    double dc_V,
    const TestTone tones[],
    size_t tone_count,
    double expected_upp_V,
    double expected_urms_V,
    SignalStatus expected_status,
    uint32_t expected_recommended_sample_rate_Hz)
{
    SignalResult result;
    const size_t expected_component_count = desired_tone_count(
        tones,
        tone_count);
    size_t tone_index;
    size_t component_index = 0U;

    generate_quantized_codes(
        sample_rate_Hz,
        dc_V,
        tones,
        tone_count);
    (void)memset(&result, 0xA5, sizeof(result));

    if (SignalAnalyzer_Process(g_samples, sample_rate_Hz, &result)
        != expected_status)
    {
        (void)fprintf(
            stderr,
            "FAIL case=%s status expected=%u actual=%u\n",
            name,
            (unsigned int)expected_status,
            (unsigned int)result.status);
        ++g_failures;
        return;
    }

    CHECK_U32(expected_status, result.status);
    CHECK_U32(sample_rate_Hz, result.sample_rate_Hz);
    CHECK_U32(expected_component_count, result.component_count);
    CHECK_U32(
        expected_recommended_sample_rate_Hz,
        result.recommended_sample_rate_Hz);
    CHECK_NEAR(tones[0].frequency_Hz, result.fundamental_Hz, 500.0);
    CHECK_NEAR(expected_upp_V, result.upp_V, 0.010);
    CHECK_NEAR(expected_urms_V, result.urms_V, 0.005);
    CHECK_TRUE(isfinite(result.residual_rms_V));
    CHECK_TRUE(result.residual_rms_V >= 0.0f);
    CHECK_TRUE(isfinite(result.snr_dB));

    for (tone_index = 0U; tone_index < tone_count; ++tone_index)
    {
        if (tones[tone_index].desired == 0U)
        {
            continue;
        }

        CHECK_TRUE(component_index < result.component_count);
        if (component_index < result.component_count)
        {
            CHECK_NEAR(
                tones[tone_index].frequency_Hz,
                result.component[component_index].frequency_Hz,
                500.0);
            CHECK_NEAR(
                tones[tone_index].amplitude_peak_V,
                result.component[component_index].amplitude_peak_V,
                0.005);
            CHECK_TRUE(phase_error(
                (double)result.component[component_index].phase_rad,
                tones[tone_index].phase_rad) <= 0.03);
        }
        ++component_index;
    }
}

static void test_end_to_end_measurement_matrix(void)
{
    static const TestTone single_10k[] = {
        {10000.0, 0.40, 0.37, 1U}
    };
    static const TestTone two_100k[] = {
        {100000.0, 0.32, 0.20, 1U},
        {200000.0, 0.11, -0.70, 1U}
    };
    static const TestTone three_125k[] = {
        {125000.0, 0.18, -1.10, 1U},
        {375000.0, 0.34, 0.40, 1U},
        {500000.0, 0.12, 2.20, 1U}
    };
    static const TestTone two_100k_phase_b[] = {
        {100000.0, 0.32, -2.31, 1U},
        {200000.0, 0.11, 1.47, 1U}
    };
    static const TestTone three_125k_phase_b[] = {
        {125000.0, 0.18, 2.73, 1U},
        {375000.0, 0.34, -0.91, 1U},
        {500000.0, 0.12, 1.88, 1U}
    };
    static const TestTone single_500k[] = {
        {500000.0, 0.25, -0.20, 1U}
    };

    check_end_to_end_case(
        "single_10k",
        2048780U,
        0.15,
        single_10k,
        1U,
        0.8000000000,
        0.2828427125,
        SIGNAL_STATUS_VALID,
        0U);
    check_end_to_end_case(
        "two_100k",
        2048780U,
        -0.12,
        two_100k,
        2U,
        0.7399756335,
        0.2392697223,
        SIGNAL_STATUS_VALID,
        0U);
    check_end_to_end_case(
        "three_125k",
        2048780U,
        0.08,
        three_125k,
        3U,
        0.9759525657,
        0.2849561370,
        SIGNAL_STATUS_VALID,
        0U);
    check_end_to_end_case(
        "two_100k_phase_b",
        2048780U,
        0.03,
        two_100k_phase_b,
        2U,
        0.6767057982,
        0.2392697223,
        SIGNAL_STATUS_VALID,
        0U);
    check_end_to_end_case(
        "three_125k_phase_b",
        2048780U,
        -0.04,
        three_125k_phase_b,
        3U,
        0.9409840438,
        0.2849561370,
        SIGNAL_STATUS_VALID,
        0U);
    check_end_to_end_case(
        "single_500k",
        2048780U,
        0.0,
        single_500k,
        1U,
        0.5000000000,
        0.1767766953,
        SIGNAL_STATUS_VALID,
        0U);
}

static void test_high_rate_interference_is_fitted_but_not_reported(void)
{
    static const TestTone tones[] = {
        {100000.0, 0.30, 0.10, 1U},
        {200000.0, 0.12, -0.40, 1U},
        {1100000.0, 0.10, 0.70, 0U}
    };

    check_end_to_end_case(
        "high_rate_interference",
        10000000U,
        0.05,
        tones,
        3U,
        0.6918169282,
        0.2284731932,
        SIGNAL_STATUS_VALID,
        0U);
}

static void test_observable_sample_rate_risks_return_recommendations(void)
{
    static const TestTone desired_only[] = {
        {100000.0, 0.32, 0.20, 1U},
        {200000.0, 0.11, -0.70, 1U}
    };
    static const TestTone with_interference[] = {
        {100000.0, 0.30, 0.10, 1U},
        {200000.0, 0.12, -0.40, 1U},
        {1100000.0, 0.10, 0.70, 0U}
    };

    check_end_to_end_case(
        "low_full_band_rate",
        800000U,
        0.0,
        desired_only,
        2U,
        0.7399756335,
        0.2392697223,
        SIGNAL_STATUS_SAMPLE_RATE_UNSAFE,
        2048780U);
    check_end_to_end_case(
        "low_interference_rate",
        2500000U,
        0.0,
        with_interference,
        3U,
        0.6918169282,
        0.2284731932,
        SIGNAL_STATUS_SAMPLE_RATE_UNSAFE,
        10000000U);
}

static void test_out_of_desired_band_tone_is_model_invalid(void)
{
    static const TestTone tone[] = {
        {700000.0, 0.30, 0.20, 0U}
    };
    SignalResult result;

    generate_quantized_codes(2048780U, 0.0, tone, 1U);
    CHECK_U32(
        SIGNAL_STATUS_MODEL_INVALID,
        SignalAnalyzer_Process(g_samples, 2048780U, &result));
    CHECK_U32(SIGNAL_STATUS_MODEL_INVALID, result.status);
    CHECK_U32(0U, result.component_count);
    CHECK_U32(0U, result.recommended_sample_rate_Hz);
}

static void test_metrics_use_only_desired_harmonics(void)
{
    HarmonicModel model = {
        4U,
        {125000.0f, 375000.0f, 500000.0f, 1100000.0f},
        {1U, 1U, 1U, 0U}
    };
    OrthogonalFitResult fit = {0};
    SignalResult result = {0};

    fit.tone[0] = (SignalComponent){125000.0f, 0.18f, -1.10f};
    fit.tone[1] = (SignalComponent){375000.0f, 0.34f, 0.40f};
    fit.tone[2] = (SignalComponent){500000.0f, 0.12f, 2.20f};
    fit.tone[3] = (SignalComponent){1100000.0f, 0.90f, -0.30f};
    fit.residual_rms_V = 0.001;
    fit.snr_dB = 55.0;

    CHECK_TRUE(SignalMetrics_Populate(&model, &fit, &result));
    CHECK_U32(3U, result.component_count);
    CHECK_NEAR(125000.0, result.fundamental_Hz, 0.01);
    CHECK_NEAR(0.9759525657, result.upp_V, 0.0001);
    CHECK_NEAR(0.2849561370, result.urms_V, 0.00001);
    CHECK_NEAR(0.001, result.residual_rms_V, 1.0e-9);
    CHECK_NEAR(55.0, result.snr_dB, 1.0e-9);
}

static void test_metrics_reject_invalid_models_and_fit_values(void)
{
    HarmonicModel model = {
        1U,
        {100000.0f, 0.0f, 0.0f, 0.0f},
        {1U, 0U, 0U, 0U}
    };
    OrthogonalFitResult fit = {0};
    SignalResult result = {0};

    fit.tone[0] = (SignalComponent){100000.0f, 0.30f, 0.20f};
    fit.residual_rms_V = 0.001;
    fit.snr_dB = 40.0;

    CHECK_TRUE(!SignalMetrics_Populate(NULL, &fit, &result));
    CHECK_TRUE(!SignalMetrics_Populate(&model, NULL, &result));
    CHECK_TRUE(!SignalMetrics_Populate(&model, &fit, NULL));

    model.desired[0] = 0U;
    CHECK_TRUE(!SignalMetrics_Populate(&model, &fit, &result));
    model.desired[0] = 1U;
    fit.tone[0].amplitude_peak_V = -0.1f;
    CHECK_TRUE(!SignalMetrics_Populate(&model, &fit, &result));
    fit.tone[0].amplitude_peak_V = 0.30f;
    fit.snr_dB = NAN;
    CHECK_TRUE(!SignalMetrics_Populate(&model, &fit, &result));
}

static void test_lower_clipped_sine_is_recovered_in_bnc_units(void)
{
    static const TestTone tone[] = {
        {100123.0, 0.125, 0.31, 1U}
    };
    SignalResult result;

    generate_quantized_codes(
        2048780U,
        15.0 * TEST_AD9226_ADC_V_PER_CODE,
        tone,
        1U);
    clamp_codes_to_lower_platform();

    CHECK_U32(
        SIGNAL_STATUS_VALID,
        SignalAnalyzer_Process(g_samples, 2048780U, &result));
    CHECK_TRUE((result.quality_flags
        & SIGNAL_QUALITY_LOWER_CLIP_DETECTED) != 0U);
    CHECK_TRUE((result.quality_flags
        & SIGNAL_QUALITY_CLIPPED_MODEL_RECOVERED) != 0U);
    CHECK_NEAR(100123.0, result.fundamental_Hz, 1000.0);
    CHECK_NEAR(0.250, result.upp_V, 0.005);
    CHECK_NEAR(0.125, result.component[0].amplitude_peak_V, 0.005);
}

static void test_lower_clipped_third_harmonic_is_not_confused_with_clip_spurs(void)
{
    static const TestTone tones[] = {
        {100123.0, 0.080 * TEST_CLIPPED_FIXTURE_SCALE, 0.31, 1U},
        {300369.0, 0.030 * TEST_CLIPPED_FIXTURE_SCALE, -0.72, 1U}
    };
    SignalResult result;
    double expected_upp_V;
    double expected_urms_V;

    calculate_expected_metrics(
        tones,
        sizeof(tones) / sizeof(tones[0]),
        &expected_upp_V,
        &expected_urms_V);
    generate_quantized_codes(
        2048780U,
        15.0 * TEST_AD9226_ADC_V_PER_CODE,
        tones,
        sizeof(tones) / sizeof(tones[0]));
    clamp_codes_to_lower_platform();

    CHECK_U32(
        SIGNAL_STATUS_VALID,
        SignalAnalyzer_Process(g_samples, 2048780U, &result));
    CHECK_U32(2U, result.component_count);
    CHECK_TRUE((result.quality_flags
        & SIGNAL_QUALITY_CLIPPED_MODEL_RECOVERED) != 0U);
    CHECK_NEAR(expected_upp_V, result.upp_V, 0.005);
    CHECK_NEAR(expected_urms_V, result.urms_V, 0.005);
    CHECK_NEAR(tones[0].amplitude_peak_V,
        result.component[0].amplitude_peak_V, 0.005);
    CHECK_NEAR(tones[1].amplitude_peak_V,
        result.component[1].amplitude_peak_V, 0.005);
    CHECK_NEAR(300369.0, result.component[1].frequency_Hz, 1000.0);
}

static void test_lower_clipped_three_component_model_is_recovered(void)
{
    static const TestTone tones[] = {
        {123456.0, 0.055 * TEST_CLIPPED_FIXTURE_SCALE, 1.10, 1U},
        {246912.0, 0.090 * TEST_CLIPPED_FIXTURE_SCALE, -0.20, 1U},
        {493824.0, 0.025 * TEST_CLIPPED_FIXTURE_SCALE, 2.20, 1U}
    };
    SignalResult result;
    double expected_upp_V;
    double expected_urms_V;
    size_t component_index;

    calculate_expected_metrics(
        tones,
        sizeof(tones) / sizeof(tones[0]),
        &expected_upp_V,
        &expected_urms_V);
    generate_quantized_codes(
        2048780U,
        15.0 * TEST_AD9226_ADC_V_PER_CODE,
        tones,
        sizeof(tones) / sizeof(tones[0]));
    clamp_codes_to_lower_platform();

    CHECK_U32(
        SIGNAL_STATUS_VALID,
        SignalAnalyzer_Process(g_samples, 2048780U, &result));
    CHECK_U32(3U, result.component_count);
    CHECK_TRUE((result.quality_flags
        & SIGNAL_QUALITY_CLIPPED_MODEL_RECOVERED) != 0U);
    CHECK_NEAR(expected_upp_V, result.upp_V, 0.005);
    CHECK_NEAR(expected_urms_V, result.urms_V, 0.005);
    for (component_index = 0U; component_index < 3U; ++component_index)
    {
        CHECK_NEAR(
            tones[component_index].frequency_Hz,
            result.component[component_index].frequency_Hz,
            1000.0);
        CHECK_NEAR(
            tones[component_index].amplitude_peak_V,
            result.component[component_index].amplitude_peak_V,
            0.005);
    }
}

static void test_lower_clipped_model_removes_high_frequency_interference(void)
{
    static const TestTone tones[] = {
        {80000.0, 0.060, 0.40, 1U},
        {240000.0, 0.035, -1.00, 1U},
        {320000.0, 0.020, 1.70, 1U},
        {1100000.0, 0.100, -0.35, 0U}
    };
    SignalResult result;
    double expected_upp_V;
    double expected_urms_V;

    calculate_expected_metrics(
        tones,
        sizeof(tones) / sizeof(tones[0]),
        &expected_upp_V,
        &expected_urms_V);
    generate_quantized_codes(
        10000000U,
        15.0 * TEST_AD9226_ADC_V_PER_CODE,
        tones,
        sizeof(tones) / sizeof(tones[0]));
    clamp_codes_to_lower_platform();

    CHECK_U32(
        SIGNAL_STATUS_VALID,
        SignalAnalyzer_Process(g_samples, 10000000U, &result));
    CHECK_U32(3U, result.component_count);
    CHECK_TRUE((result.quality_flags
        & SIGNAL_QUALITY_CLIPPED_MODEL_RECOVERED) != 0U);
    CHECK_TRUE((result.quality_flags
        & SIGNAL_QUALITY_INTERFERENCE_REMOVED) != 0U);
    CHECK_NEAR(expected_upp_V, result.upp_V, 0.005);
    CHECK_NEAR(expected_urms_V, result.urms_V, 0.005);
    CHECK_NEAR(0.060, result.component[0].amplitude_peak_V, 0.005);
    CHECK_NEAR(0.035, result.component[1].amplitude_peak_V, 0.005);
    CHECK_NEAR(0.020, result.component[2].amplitude_peak_V, 0.005);
}

static void test_lower_clipping_with_too_few_valid_samples_is_rejected(void)
{
    static const TestTone tone[] = {
        {100123.0, 0.125, 0.31, 1U}
    };
    SignalResult result;

    generate_quantized_codes(2048780U, -0.220, tone, 1U);
    clamp_codes_to_lower_platform();

    CHECK_U32(
        SIGNAL_STATUS_CLIPPED,
        SignalAnalyzer_Process(g_samples, 2048780U, &result));
    CHECK_TRUE((result.quality_flags
        & SIGNAL_QUALITY_LOWER_CLIP_DETECTED) != 0U);
    CHECK_TRUE((result.quality_flags
        & SIGNAL_QUALITY_CLIPPED_MODEL_RECOVERED) == 0U);
}

static void test_targeted_fit_recovers_two_percent_second_harmonic(void)
{
    static const TestTone tones[] = {
        {100123.0, 0.200, 0.31, 1U},
        {200246.0, 0.004, -0.72, 1U}
    };
    SignalResult result;

    generate_quantized_codes(2048780U, 0.0, tones, 2U);
    CHECK_U32(SIGNAL_STATUS_VALID,
        SignalAnalyzer_Process(g_samples, 2048780U, &result));
    CHECK_U32(2U, result.component_count);
    CHECK_NEAR(100123.0, result.component[0].frequency_Hz, 1000.0);
    CHECK_NEAR(200246.0, result.component[1].frequency_Hz, 1000.0);
    CHECK_NEAR(0.004, result.component[1].amplitude_peak_V, 0.0015);
}

static void test_targeted_fit_recovers_weak_third_and_fourth_harmonics(void)
{
    static const TestTone tones[] = {
        {80000.0, 0.200, 0.20, 1U},
        {240000.0, 0.006, -0.80, 1U},
        {320000.0, 0.004, 1.10, 1U}
    };
    SignalResult result;

    generate_quantized_codes(2048780U, 0.0, tones, 3U);
    CHECK_U32(SIGNAL_STATUS_VALID,
        SignalAnalyzer_Process(g_samples, 2048780U, &result));
    CHECK_U32(3U, result.component_count);
    CHECK_NEAR(240000.0, result.component[1].frequency_Hz, 1000.0);
    CHECK_NEAR(320000.0, result.component[2].frequency_Hz, 1000.0);
}

static void test_targeted_fit_keeps_only_two_strongest_weak_harmonics(void)
{
    static const TestTone tones[] = {
        {80000.0, 0.200, 0.10, 1U},
        {160000.0, 0.006, 0.20, 1U},
        {240000.0, 0.005, -0.30, 1U},
        {320000.0, 0.004, 0.70, 1U}
    };
    SignalResult result;

    generate_quantized_codes(2048780U, 0.0, tones, 4U);
    CHECK_U32(SIGNAL_STATUS_VALID,
        SignalAnalyzer_Process(g_samples, 2048780U, &result));
    CHECK_U32(3U, result.component_count);
    CHECK_NEAR(160000.0, result.component[1].frequency_Hz, 1000.0);
    CHECK_NEAR(240000.0, result.component[2].frequency_Hz, 1000.0);
}

static void test_targeted_fit_does_not_invent_quantized_harmonics(void)
{
    static const TestTone pure[] = {
        {156000.0, 0.024, -1.75, 1U}
    };
    static const TestTone sub_lsb[] = {
        {100000.0, 0.200, 0.10, 1U},
        {300000.0, 0.001, 0.90, 1U}
    };
    SignalResult result;

    generate_quantized_codes(2048780U, 0.0, pure, 1U);
    CHECK_U32(SIGNAL_STATUS_VALID,
        SignalAnalyzer_Process(g_samples, 2048780U, &result));
    CHECK_U32(1U, result.component_count);

    generate_quantized_codes(2048780U, 0.0, sub_lsb, 2U);
    CHECK_U32(SIGNAL_STATUS_VALID,
        SignalAnalyzer_Process(g_samples, 2048780U, &result));
    CHECK_U32(1U, result.component_count);
}

int main(void)
{
    test_end_to_end_measurement_matrix();
    test_high_rate_interference_is_fitted_but_not_reported();
    test_observable_sample_rate_risks_return_recommendations();
    test_out_of_desired_band_tone_is_model_invalid();
    test_metrics_use_only_desired_harmonics();
    test_metrics_reject_invalid_models_and_fit_values();
    test_lower_clipped_sine_is_recovered_in_bnc_units();
    test_lower_clipped_third_harmonic_is_not_confused_with_clip_spurs();
    test_lower_clipped_three_component_model_is_recovered();
    test_lower_clipped_model_removes_high_frequency_interference();
    test_lower_clipping_with_too_few_valid_samples_is_rejected();
    test_targeted_fit_recovers_two_percent_second_harmonic();
    test_targeted_fit_recovers_weak_third_and_fourth_harmonics();
    test_targeted_fit_keeps_only_two_strongest_weak_harmonics();
    test_targeted_fit_does_not_invent_quantized_harmonics();

    if (g_failures != 0U)
    {
        (void)fprintf(
            stderr,
            "%u end-to-end test(s) failed\n",
            g_failures);
        return 1;
    }

    (void)puts("signal_end_to_end_tests: PASS");
    return 0;
}
