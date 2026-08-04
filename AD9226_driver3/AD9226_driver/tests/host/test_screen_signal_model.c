#include "screen_signal_model.h"

#include <stdint.h>
#include <stdio.h>

static unsigned int g_failures;

#define CHECK_TRUE(condition)                                                   \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            (void)fprintf(stderr, "FAIL %s:%d: %s\n",                        \
                __FILE__, __LINE__, #condition);                                \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

#define CHECK_U32(expected, actual)                                             \
    do                                                                          \
    {                                                                           \
        const uint32_t e_ = (uint32_t)(expected);                               \
        const uint32_t a_ = (uint32_t)(actual);                                 \
        if (e_ != a_)                                                           \
        {                                                                       \
            (void)fprintf(stderr,                                               \
                "FAIL %s:%d: expected %lu, got %lu\n",                       \
                __FILE__, __LINE__, (unsigned long)e_, (unsigned long)a_);      \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

static uint32_t wave_pp_uV(const int32_t *wave, size_t count)
{
    int32_t minimum = wave[0U];
    int32_t maximum = wave[0U];
    size_t index;

    for (index = 1U; index < count; ++index)
    {
        if (wave[index] < minimum)
        {
            minimum = wave[index];
        }
        if (wave[index] > maximum)
        {
            maximum = wave[index];
        }
    }
    return (uint32_t)(maximum - minimum);
}

static void test_one_and_three_period_waveforms_match_model(void)
{
    H743MeasurementModel model = {0};
    int32_t wave1[SCREEN_SIGNAL_POINTS_PER_PERIOD];
    int32_t wave3[3U * SCREEN_SIGNAL_POINTS_PER_PERIOD];
    size_t index;

    model.fundamental_Hz = 100000U;
    model.upp_uV = 250000U;
    model.component_count = 1U;
    model.component[0] = (H743MeasurementComponent){1U, 125000U, 0, 1U};

    CHECK_TRUE(ScreenSignalModel_GenerateWave(
        &model, 1U, wave1, SCREEN_SIGNAL_POINTS_PER_PERIOD));
    CHECK_TRUE((wave_pp_uV(wave1, SCREEN_SIGNAL_POINTS_PER_PERIOD)
        > (model.upp_uV - 1000U))
        && (wave_pp_uV(wave1, SCREEN_SIGNAL_POINTS_PER_PERIOD)
            < (model.upp_uV + 1000U)));

    model.component_count = 3U;
    model.component[0] = (H743MeasurementComponent){1U, 100000U, 0, 1U};
    model.component[1] = (H743MeasurementComponent){3U, 30000U, 16384, 1U};
    model.component[2] = (H743MeasurementComponent){4U, 20000U, -16384, 1U};
    CHECK_TRUE(ScreenSignalModel_GenerateWave(
        &model, 1U, wave1, SCREEN_SIGNAL_POINTS_PER_PERIOD));
    CHECK_TRUE(ScreenSignalModel_GenerateWave(
        &model, 3U, wave3, 3U * SCREEN_SIGNAL_POINTS_PER_PERIOD));

    for (index = 0U; index < 3U * SCREEN_SIGNAL_POINTS_PER_PERIOD; ++index)
    {
        CHECK_U32((uint32_t)wave1[index % SCREEN_SIGNAL_POINTS_PER_PERIOD],
            (uint32_t)wave3[index]);
    }
}

static void test_wave_codes_keep_zero_volts_at_midscale(void)
{
    H743MeasurementModel model = {0};
    uint16_t codes[SCREEN_SIGNAL_POINTS_PER_PERIOD];

    model.fundamental_Hz = 100000U;
    model.component_count = 1U;
    model.component[0] =
        (H743MeasurementComponent){1U, 100000U, 0, 1U};

    CHECK_TRUE(ScreenSignalModel_GenerateWaveCodes(
        &model, 1U, codes, SCREEN_SIGNAL_POINTS_PER_PERIOD));
    CHECK_U32(4095U, codes[0U]);
    CHECK_TRUE((codes[64U] >= 2047U) && (codes[64U] <= 2049U));
    CHECK_U32(1U, codes[128U]);
    CHECK_TRUE((codes[192U] >= 2047U) && (codes[192U] <= 2049U));
}

static void test_wave_codes_repeat_and_reject_invalid_output(void)
{
    H743MeasurementModel model = {0};
    uint16_t codes[3U * SCREEN_SIGNAL_POINTS_PER_PERIOD];
    size_t index;

    model.fundamental_Hz = 80000U;
    model.component_count = 2U;
    model.component[0] =
        (H743MeasurementComponent){1U, 100000U, 0, 1U};
    model.component[1] =
        (H743MeasurementComponent){3U, 30000U, 8192, 1U};

    CHECK_TRUE(ScreenSignalModel_GenerateWaveCodes(
        &model, 3U, codes,
        3U * SCREEN_SIGNAL_POINTS_PER_PERIOD));
    for (index = SCREEN_SIGNAL_POINTS_PER_PERIOD;
         index < 3U * SCREEN_SIGNAL_POINTS_PER_PERIOD;
         ++index)
    {
        CHECK_U32(codes[index % SCREEN_SIGNAL_POINTS_PER_PERIOD],
            codes[index]);
    }

    codes[0U] = 1234U;
    CHECK_TRUE(!ScreenSignalModel_GenerateWaveCodes(
        &model, 3U, codes, SCREEN_SIGNAL_POINTS_PER_PERIOD));
    CHECK_U32(1234U, codes[0U]);
}

static void test_spectrum_uses_peak_amplitude_and_real_frequency(void)
{
    H743MeasurementModel model = {0};
    ScreenSignalSpectrum spectrum;

    model.fundamental_Hz = 100000U;
    model.component_count = 3U;
    model.component[0] = (H743MeasurementComponent){1U, 100000U, 0, 1U};
    model.component[1] = (H743MeasurementComponent){3U, 30000U, 0, 1U};
    model.component[2] = (H743MeasurementComponent){4U, 20000U, 0, 1U};

    CHECK_TRUE(ScreenSignalModel_FillSpectrum(&model, &spectrum));
    CHECK_U32(3U, spectrum.count);
    CHECK_U32(100000000U, spectrum.line[0U].frequency_mHz);
    CHECK_U32(300000000U, spectrum.line[1U].frequency_mHz);
    CHECK_U32(30000U, spectrum.line[1U].amplitude_peak_uV);
    CHECK_U32(400000000U, spectrum.line[2U].frequency_mHz);
}

static void test_spectrum_rasterizer_draws_exactly_one_bin_per_component(void)
{
    ScreenSignalSpectrum spectrum = {0};
    uint16_t bins[256];
    size_t index;
    size_t nonzero_count = 0U;

    spectrum.count = 1U;
    spectrum.line[0U].frequency_mHz = 100000000U;
    spectrum.line[0U].amplitude_peak_uV = 125000U;

    CHECK_TRUE(ScreenSignalModel_RasterizeSpectrum(
        &spectrum, 500000000U, bins, 256U));
    for (index = 0U; index < 256U; ++index)
    {
        if (bins[index] != 0U)
        {
            ++nonzero_count;
        }
    }
    CHECK_U32(1U, nonzero_count);
    CHECK_U32(0U, bins[50U]);
    CHECK_U32(125U, bins[51U]);
    CHECK_U32(0U, bins[52U]);
}

static void test_invalid_models_are_rejected_without_writing_output(void)
{
    H743MeasurementModel model = {0};
    int32_t wave[SCREEN_SIGNAL_POINTS_PER_PERIOD];
    ScreenSignalSpectrum spectrum;

    model.fundamental_Hz = 100000U;
    model.component_count = 1U;
    model.component[0] = (H743MeasurementComponent){0U, 100000U, 0, 1U};
    wave[0U] = 12345;
    spectrum.count = 99U;

    CHECK_TRUE(!ScreenSignalModel_GenerateWave(
        &model, 1U, wave, SCREEN_SIGNAL_POINTS_PER_PERIOD));
    CHECK_U32(12345U, (uint32_t)wave[0U]);
    CHECK_TRUE(!ScreenSignalModel_FillSpectrum(&model, &spectrum));
    CHECK_U32(99U, spectrum.count);
}

int main(void)
{
    test_one_and_three_period_waveforms_match_model();
    test_wave_codes_keep_zero_volts_at_midscale();
    test_wave_codes_repeat_and_reject_invalid_output();
    test_spectrum_uses_peak_amplitude_and_real_frequency();
    test_spectrum_rasterizer_draws_exactly_one_bin_per_component();
    test_invalid_models_are_rejected_without_writing_output();
    if (g_failures != 0U)
    {
        (void)fprintf(stderr, "%u screen model test(s) failed\n", g_failures);
        return 1;
    }
    (void)puts("screen_signal_model_tests: PASS");
    return 0;
}
