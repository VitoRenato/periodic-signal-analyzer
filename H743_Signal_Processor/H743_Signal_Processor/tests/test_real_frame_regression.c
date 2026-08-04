#include "ad9226_code_normalizer.h"
#include "signal_analyzer.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_SAMPLE_RATE_HZ 2048780U
#define TEST_ANCHOR_CODE 2055U

static uint16_t g_raw_samples[SIGNAL_FRAME_SAMPLES];
static uint16_t g_normalized_samples[SIGNAL_FRAME_SAMPLES];
static unsigned int g_failures;

#define CHECK_TRUE(condition)                                                   \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            (void)fprintf(stderr, "FAIL %s:%d: %s\n",                         \
                          __FILE__, __LINE__, #condition);                       \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

#define CHECK_NEAR(expected, actual, tolerance)                                 \
    do                                                                          \
    {                                                                           \
        const double expected_value = (double)(expected);                       \
        const double actual_value = (double)(actual);                           \
        const double difference = fabs(actual_value - expected_value);          \
        if (!isfinite(actual_value) || (difference > (double)(tolerance)))       \
        {                                                                       \
            (void)fprintf(stderr,                                               \
                          "FAIL %s:%d: expected %.3f got %.3f error %.3f\n",   \
                          __FILE__, __LINE__, expected_value,                    \
                          actual_value, difference);                            \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

static int Test_LoadFixturePath(
    const char *path,
    uint16_t samples[SIGNAL_FRAME_SAMPLES])
{
    FILE *file = fopen(path, "rb");
    size_t count;

    if (file == NULL)
    {
        (void)fprintf(stderr, "cannot open fixture: %s\n", path);
        return 0;
    }
    count = fread(
        samples,
        sizeof(samples[0]),
        SIGNAL_FRAME_SAMPLES,
        file);
    if (fclose(file) != 0)
    {
        return 0;
    }
    return count == SIGNAL_FRAME_SAMPLES;
}

static int Test_LoadFixture(void)
{
    return Test_LoadFixturePath(AD9226_REAL_FRAME_FIXTURE, g_raw_samples);
}

static void Test_NormalizedFrameSelectsRealFundamental(void)
{
    Ad9226CodeNormalizeStats normalize_stats;
    SignalResult raw_result;
    SignalResult normalized_result;
    SignalStatus raw_status;
    SignalStatus normalized_status;
    size_t component_index;

    CHECK_TRUE(Test_LoadFixture());
    (void)memcpy(g_normalized_samples, g_raw_samples, sizeof(g_raw_samples));

    (void)memset(&raw_result, 0, sizeof(raw_result));
    raw_status = SignalAnalyzer_Process(
        g_raw_samples,
        TEST_SAMPLE_RATE_HZ,
        &raw_result);
    CHECK_TRUE(!((raw_status == SIGNAL_STATUS_VALID)
        && (raw_result.fundamental_Hz >= 99000.0f)
        && (raw_result.fundamental_Hz <= 101000.0f)
        && (raw_result.component_count == 1U)));

    CHECK_TRUE(Ad9226CodeNormalizer_Run(
        g_normalized_samples,
        SIGNAL_FRAME_SAMPLES,
        TEST_ANCHOR_CODE,
        &normalize_stats));
    CHECK_TRUE(normalize_stats.ambiguous == 0U);
    CHECK_TRUE(normalize_stats.normalized_min == 2040U);
    CHECK_TRUE(normalize_stats.normalized_max == 2144U);

    (void)memset(&normalized_result, 0, sizeof(normalized_result));
    normalized_status = SignalAnalyzer_Process(
        g_normalized_samples,
        TEST_SAMPLE_RATE_HZ,
        &normalized_result);
    CHECK_TRUE(normalized_status == SIGNAL_STATUS_VALID);
    CHECK_NEAR(100000.0, normalized_result.fundamental_Hz, 1000.0);
    CHECK_TRUE((normalized_result.component_count >= 1U)
        && (normalized_result.component_count <= SIGNAL_MAX_COMPONENTS));
    CHECK_NEAR(
        normalized_result.fundamental_Hz,
        normalized_result.component[0].frequency_Hz,
        1000.0);
    for (component_index = 1U;
         component_index < (size_t)normalized_result.component_count;
         ++component_index)
    {
        CHECK_TRUE(normalized_result.component[component_index].frequency_Hz
            > normalized_result.component[component_index - 1U].frequency_Hz);
    }

    (void)printf(
        "raw_status=%u raw_f0=%.1f normalized_status=%u normalized_f0=%.1f components=%u\n",
        (unsigned)raw_status,
        (double)raw_result.fundamental_Hz,
        (unsigned)normalized_status,
        (double)normalized_result.fundamental_Hz,
        (unsigned)normalized_result.component_count);
}

static void Test_CurrentCaptureRemainsSingleComponent(void)
{
    SignalResult result;

    CHECK_TRUE(Test_LoadFixturePath(
        AD9226_CURRENT_CAPTURE_FIXTURE,
        g_normalized_samples));
    CHECK_TRUE(SignalAnalyzer_Process(
        g_normalized_samples,
        TEST_SAMPLE_RATE_HZ,
        &result) == SIGNAL_STATUS_VALID);
    CHECK_NEAR(155995.5, result.fundamental_Hz, 1000.0);
    CHECK_TRUE(result.component_count == 1U);
}

int main(void)
{
    Test_NormalizedFrameSelectsRealFundamental();
    Test_CurrentCaptureRemainsSingleComponent();
    if (g_failures != 0U)
    {
        (void)fprintf(stderr, "%u failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
