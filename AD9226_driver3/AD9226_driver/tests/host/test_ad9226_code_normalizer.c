#include "ad9226_code_normalizer.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define TEST_SAMPLE_COUNT 4096U
#define TEST_ANCHOR_CODE 2055U

static int g_failures;
static uint16_t g_samples[TEST_SAMPLE_COUNT];

#define CHECK_TRUE(expr)                                                     \
    do                                                                       \
    {                                                                        \
        if (!(expr))                                                         \
        {                                                                    \
            (void)fprintf(stderr, "FAIL %s:%d: %s\n",                       \
                          __FILE__, __LINE__, #expr);                         \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_U16(expected, actual)                                          \
    do                                                                       \
    {                                                                        \
        const uint16_t expected_value = (uint16_t)(expected);                \
        const uint16_t actual_value = (uint16_t)(actual);                    \
        if (expected_value != actual_value)                                  \
        {                                                                    \
            (void)fprintf(stderr,                                            \
                          "FAIL %s:%d: expected %u, got %u\n",              \
                          __FILE__, __LINE__,                                \
                          (unsigned)expected_value,                          \
                          (unsigned)actual_value);                           \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

static uint16_t Test_CaptureLowByte(uint16_t continuous_code)
{
    return (uint16_t)(0x0800U | (continuous_code & 0x00ffU));
}

static void Test_RealFrame(void)
{
    Ad9226CodeNormalizeStats stats;
    FILE *file = fopen(AD9226_REAL_FRAME_FIXTURE, "rb");
    size_t count;
    uint16_t raw_min = 0xffffU;
    uint16_t raw_max = 0U;
    size_t index;

    CHECK_TRUE(file != NULL);
    if (file == NULL)
    {
        return;
    }
    count = fread(g_samples, sizeof(g_samples[0]), TEST_SAMPLE_COUNT, file);
    CHECK_TRUE(fclose(file) == 0);
    CHECK_TRUE(count == TEST_SAMPLE_COUNT);
    if (count != TEST_SAMPLE_COUNT)
    {
        return;
    }

    for (index = 0U; index < count; ++index)
    {
        if (g_samples[index] < raw_min) raw_min = g_samples[index];
        if (g_samples[index] > raw_max) raw_max = g_samples[index];
    }
    CHECK_U16(2048U, raw_min);
    CHECK_U16(2303U, raw_max);

    CHECK_TRUE(Ad9226CodeNormalizer_Run(
        g_samples, count, TEST_ANCHOR_CODE, &stats));
    CHECK_U16(2048U, stats.raw_min);
    CHECK_U16(2303U, stats.raw_max);
    CHECK_U16(2040U, stats.normalized_min);
    CHECK_U16(2144U, stats.normalized_max);
    CHECK_U16(0U, stats.ambiguous);
    CHECK_TRUE(stats.max_step < 128U);

    for (index = 1U; index < count; ++index)
    {
        const int step = (int)g_samples[index] - (int)g_samples[index - 1U];
        CHECK_TRUE(abs(step) < 128);
    }
}

static void Test_CrossingIsRestoredExactly(void)
{
    static const uint16_t expected[] = {
        2140U, 2100U, 2057U, 2048U, 2043U, 2047U, 2055U, 2067U
    };
    uint16_t captured[sizeof(expected) / sizeof(expected[0])];
    Ad9226CodeNormalizeStats stats;
    size_t index;

    for (index = 0U; index < (sizeof(expected) / sizeof(expected[0])); ++index)
    {
        captured[index] = Test_CaptureLowByte(expected[index]);
    }

    CHECK_TRUE(Ad9226CodeNormalizer_Run(
        captured,
        sizeof(captured) / sizeof(captured[0]),
        TEST_ANCHOR_CODE,
        &stats));
    for (index = 0U; index < (sizeof(expected) / sizeof(expected[0])); ++index)
    {
        CHECK_U16(expected[index], captured[index]);
    }
    CHECK_U16(0U, stats.ambiguous);
}

static void Test_FrameIsCenteredWithoutChangingAc(void)
{
    static const uint16_t original[] = {2290U, 2300U, 2310U, 2295U};
    uint16_t captured[sizeof(original) / sizeof(original[0])];
    Ad9226CodeNormalizeStats stats;
    size_t index;

    for (index = 0U; index < (sizeof(original) / sizeof(original[0])); ++index)
    {
        captured[index] = Test_CaptureLowByte(original[index]);
    }
    CHECK_TRUE(Ad9226CodeNormalizer_Run(
        captured,
        sizeof(captured) / sizeof(captured[0]),
        TEST_ANCHOR_CODE,
        &stats));
    for (index = 1U; index < (sizeof(original) / sizeof(original[0])); ++index)
    {
        CHECK_TRUE(((int)captured[index] - (int)captured[0])
            == ((int)original[index] - (int)original[0]));
    }
    CHECK_TRUE(abs((int)captured[0] - (int)TEST_ANCHOR_CODE) < 128);
}

static void Test_AmbiguousHalfTurnIsRejected(void)
{
    uint16_t captured[2] = {0x0800U, 0x0880U};
    Ad9226CodeNormalizeStats stats;

    CHECK_TRUE(!Ad9226CodeNormalizer_Run(
        captured, 2U, TEST_ANCHOR_CODE, &stats));
    CHECK_U16(1U, stats.ambiguous);
}

static void Test_InvalidArgumentsAreRejected(void)
{
    uint16_t sample = 2055U;
    Ad9226CodeNormalizeStats stats;

    CHECK_TRUE(!Ad9226CodeNormalizer_Run(NULL, 1U, TEST_ANCHOR_CODE, &stats));
    CHECK_TRUE(!Ad9226CodeNormalizer_Run(&sample, 0U, TEST_ANCHOR_CODE, &stats));
    CHECK_TRUE(!Ad9226CodeNormalizer_Run(&sample, 1U, 4096U, &stats));
    CHECK_TRUE(!Ad9226CodeNormalizer_Run(&sample, 1U, TEST_ANCHOR_CODE, NULL));
}

int main(void)
{
    Test_RealFrame();
    Test_CrossingIsRestoredExactly();
    Test_FrameIsCenteredWithoutChangingAc();
    Test_AmbiguousHalfTurnIsRejected();
    Test_InvalidArgumentsAreRejected();

    if (g_failures != 0)
    {
        (void)fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    (void)printf("ad9226 code normalizer tests passed\n");
    return 0;
}
