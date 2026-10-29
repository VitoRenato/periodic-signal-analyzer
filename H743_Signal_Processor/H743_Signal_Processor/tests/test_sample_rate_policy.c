#include "sample_rate_policy.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr)                                                          \
    do                                                                       \
    {                                                                        \
        if (!(expr))                                                         \
        {                                                                    \
            (void)fprintf(stderr, "FAIL %s:%d: %s\n",                        \
                          __FILE__, __LINE__, #expr);                        \
            abort();                                                         \
        }                                                                    \
    } while (0)

static SignalResult make_result(
    SignalStatus status,
    float f0_Hz,
    uint32_t recommended_Hz,
    uint8_t quality_flags)
{
    SignalResult result = {0};
    result.status = status;
    result.fundamental_Hz = f0_Hz;
    result.recommended_sample_rate_Hz = recommended_Hz;
    result.quality_flags = quality_flags;
    return result;
}

static void test_step_up_revert_and_no_oscillation_in_same_session(void)
{
    SampleRatePolicy policy;
    SignalResult result;

    SampleRatePolicy_Init(&policy);
    CHECK(policy.requested_sample_rate_Hz == SAMPLE_RATE_POLICY_DEFAULT_REQUEST_HZ);

    /* 2.048 MHz 下检测到干扰且 f0 >= 50 kHz：升到 10 MHz。 */
    result = make_result(
        SIGNAL_STATUS_SAMPLE_RATE_UNSAFE,
        100000.0f,
        SAMPLE_RATE_POLICY_HIGH_REQUEST_HZ,
        SIGNAL_QUALITY_INTERFERENCE_REMOVED);
    CHECK(SampleRatePolicy_Next(&policy, 2048780U, &result)
          == SAMPLE_RATE_POLICY_HIGH_REQUEST_HZ);

    /* 在 10 MHz 下仍有干扰时保持 10 MHz，清零干净帧计数。 */
    result = make_result(
        SIGNAL_STATUS_VALID,
        100000.0f,
        0U,
        SIGNAL_QUALITY_INTERFERENCE_REMOVED);
    CHECK(SampleRatePolicy_Next(&policy, 10500000U, &result)
          == SAMPLE_RATE_POLICY_HIGH_REQUEST_HZ);

    /* 连续 3 帧有效且无干扰后退回 2.048 MHz。 */
    result = make_result(SIGNAL_STATUS_VALID, 100000.0f, 0U, 0U);
    CHECK(SampleRatePolicy_Next(&policy, 10500000U, &result)
          == SAMPLE_RATE_POLICY_HIGH_REQUEST_HZ);
    CHECK(SampleRatePolicy_Next(&policy, 10500000U, &result)
          == SAMPLE_RATE_POLICY_HIGH_REQUEST_HZ);
    CHECK(SampleRatePolicy_Next(&policy, 10500000U, &result)
          == SAMPLE_RATE_POLICY_DEFAULT_REQUEST_HZ);

    /* 同轮测量内即使再次看到混叠引起的推荐升频，也不再升回 10 MHz，防止振荡。 */
    result = make_result(
        SIGNAL_STATUS_SAMPLE_RATE_UNSAFE,
        345000.0f,
        SAMPLE_RATE_POLICY_HIGH_REQUEST_HZ,
        SIGNAL_QUALITY_INTERFERENCE_REMOVED);
    CHECK(SampleRatePolicy_Next(&policy, 2048780U, &result)
          == SAMPLE_RATE_POLICY_DEFAULT_REQUEST_HZ);

    /* 新一轮测量（F407 带 REARM/BOOT）后允许再次升频。 */
    SampleRatePolicy_OnNewSession(&policy);
    CHECK(SampleRatePolicy_Next(&policy, 2048780U, &result)
          == SAMPLE_RATE_POLICY_HIGH_REQUEST_HZ);
}

static void test_low_fundamental_stays_at_default_rate(void)
{
    SampleRatePolicy policy;
    SignalResult result;

    SampleRatePolicy_Init(&policy);
    /* 10 kHz 在 10 MHz 下单帧只有约 4 个周期，不升频。 */
    result = make_result(
        SIGNAL_STATUS_SAMPLE_RATE_UNSAFE,
        10000.0f,
        SAMPLE_RATE_POLICY_HIGH_REQUEST_HZ,
        SIGNAL_QUALITY_INTERFERENCE_REMOVED);
    CHECK(SampleRatePolicy_Next(&policy, 2048780U, &result)
          == SAMPLE_RATE_POLICY_DEFAULT_REQUEST_HZ);
}

static void test_nonconsecutive_clean_frames_do_not_revert(void)
{
    SampleRatePolicy policy;
    SignalResult result;

    SampleRatePolicy_Init(&policy);
    result = make_result(
        SIGNAL_STATUS_SAMPLE_RATE_UNSAFE,
        100000.0f,
        SAMPLE_RATE_POLICY_HIGH_REQUEST_HZ,
        SIGNAL_QUALITY_INTERFERENCE_REMOVED);
    CHECK(SampleRatePolicy_Next(&policy, 2048780U, &result)
          == SAMPLE_RATE_POLICY_HIGH_REQUEST_HZ);

    /* 10 MHz 下：valid, no_signal, valid, valid 必须重新计数。 */
    result = make_result(SIGNAL_STATUS_VALID, 100000.0f, 0U, 0U);
    CHECK(SampleRatePolicy_Next(&policy, 10500000U, &result)
          == SAMPLE_RATE_POLICY_HIGH_REQUEST_HZ);
    result = make_result(SIGNAL_STATUS_NO_SIGNAL, 0.0f, 0U, 0U);
    CHECK(SampleRatePolicy_Next(&policy, 10500000U, &result)
          == SAMPLE_RATE_POLICY_HIGH_REQUEST_HZ);
    result = make_result(SIGNAL_STATUS_VALID, 100000.0f, 0U, 0U);
    CHECK(SampleRatePolicy_Next(&policy, 10500000U, &result)
          == SAMPLE_RATE_POLICY_HIGH_REQUEST_HZ);
    CHECK(SampleRatePolicy_Next(&policy, 10500000U, &result)
          == SAMPLE_RATE_POLICY_HIGH_REQUEST_HZ);
    CHECK(!policy.reverted_in_session);
}

int main(void)
{
    test_step_up_revert_and_no_oscillation_in_same_session();
    test_low_fundamental_stays_at_default_rate();
    test_nonconsecutive_clean_frames_do_not_revert();
    puts("sample_rate_policy_tests: PASS");
    return 0;
}
