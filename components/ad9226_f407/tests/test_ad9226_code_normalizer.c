#include "ad9226_code_normalizer.h"

#include <stdio.h>

static int g_failures;

#define CHECK_TRUE(expression)                                             \
    do                                                                     \
    {                                                                      \
        if (!(expression))                                                 \
        {                                                                  \
            (void)fprintf(stderr, "FAIL %s:%d: %s\\n",                 \
                          __FILE__, __LINE__, #expression);               \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

#define CHECK_U16(expected, actual)                                        \
    do                                                                     \
    {                                                                      \
        const uint16_t expected_value = (uint16_t)(expected);              \
        const uint16_t actual_value = (uint16_t)(actual);                  \
        if (expected_value != actual_value)                                \
        {                                                                  \
            (void)fprintf(stderr,                                          \
                          "FAIL %s:%d: expected %u, got %u\\n",          \
                          __FILE__, __LINE__,                              \
                          (unsigned)expected_value,                        \
                          (unsigned)actual_value);                         \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

static void test_reconstructs_low_byte_wrap_near_anchor(void)
{
    uint16_t samples[] = {250U, 254U, 2U, 6U};
    const uint16_t expected[] = {2042U, 2046U, 2050U, 2054U};
    Ad9226CodeNormalizeStats stats;
    size_t index;

    CHECK_TRUE(Ad9226CodeNormalizer_Run(samples, 4U, 2048U, &stats));
    for (index = 0U; index < 4U; ++index)
    {
        CHECK_U16(expected[index], samples[index]);
    }
    CHECK_U16(0U, stats.ambiguous);
}

static void test_recenters_complete_frame_by_256_code_steps(void)
{
    uint16_t samples[] = {0U, 127U, 254U, 125U};
    Ad9226CodeNormalizeStats stats;

    CHECK_TRUE(Ad9226CodeNormalizer_Run(samples, 4U, 2048U, &stats));
    CHECK_U16(1792U, samples[0]);
    CHECK_U16(1919U, samples[1]);
    CHECK_U16(2046U, samples[2]);
    CHECK_U16(2173U, samples[3]);
    CHECK_TRUE(stats.frame_shift == -256);
}

static void test_rejects_exact_128_code_ambiguity(void)
{
    uint16_t samples[] = {0U, 128U};
    Ad9226CodeNormalizeStats stats;

    CHECK_TRUE(!Ad9226CodeNormalizer_Run(samples, 2U, 2048U, &stats));
    CHECK_U16(1U, stats.ambiguous);
}

static void test_rejects_candidate_branch_outside_12_bit_range(void)
{
    uint16_t underflow_sample = 250U;
    uint16_t overflow_sample = 0U;
    Ad9226CodeNormalizeStats stats;

    CHECK_TRUE(!Ad9226CodeNormalizer_Run(
        &underflow_sample, 1U, 0U, &stats));
    CHECK_U16(1U, stats.ambiguous);

    CHECK_TRUE(!Ad9226CodeNormalizer_Run(
        &overflow_sample, 1U, 4095U, &stats));
    CHECK_U16(1U, stats.ambiguous);
}

static void test_rejects_null_empty_and_anchor_above_4095(void)
{
    uint16_t sample = 0U;
    Ad9226CodeNormalizeStats stats;

    CHECK_TRUE(!Ad9226CodeNormalizer_Run(NULL, 1U, 2048U, &stats));
    CHECK_TRUE(!Ad9226CodeNormalizer_Run(&sample, 0U, 2048U, &stats));
    CHECK_TRUE(!Ad9226CodeNormalizer_Run(&sample, 1U, 4096U, &stats));
    CHECK_TRUE(!Ad9226CodeNormalizer_Run(&sample, 1U, 2048U, NULL));
}

static void test_output_codes_remain_between_0_and_4095(void)
{
    uint16_t samples[] = {250U, 254U, 2U, 6U};
    Ad9226CodeNormalizeStats stats;
    size_t index;

    CHECK_TRUE(Ad9226CodeNormalizer_Run(samples, 4U, 2048U, &stats));
    for (index = 0U; index < 4U; ++index)
    {
        CHECK_TRUE(samples[index] <= 4095U);
    }
    CHECK_TRUE(stats.normalized_min <= stats.normalized_max);
}

int main(void)
{
    test_reconstructs_low_byte_wrap_near_anchor();
    test_recenters_complete_frame_by_256_code_steps();
    test_rejects_exact_128_code_ambiguity();
    test_rejects_candidate_branch_outside_12_bit_range();
    test_rejects_null_empty_and_anchor_above_4095();
    test_output_codes_remain_between_0_and_4095();

    if (g_failures != 0)
    {
        (void)fprintf(stderr, "%d failure(s)\\n", g_failures);
        return 1;
    }
    (void)printf("ad9226 code normalizer tests passed\\n");
    return 0;
}
