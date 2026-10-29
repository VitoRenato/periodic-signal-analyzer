#include "sample_rate_policy.h"

#include "h743_set_fs_protocol.h"

#include <stddef.h>

static bool SampleRatePolicy_IsHighRate(uint32_t sample_rate_Hz)
{
    return sample_rate_Hz >= 5000000U;
}

static bool SampleRatePolicy_CanStepUpToHighRate(
    const SampleRatePolicy *policy,
    const SignalResult *result)
{
    const bool fundamental_fast_enough =
        (result->fundamental_Hz >= SAMPLE_RATE_POLICY_MIN_F0_FOR_HIGH_FS_HZ);

    return !policy->reverted_in_session && fundamental_fast_enough;
}

void SampleRatePolicy_Init(SampleRatePolicy *policy)
{
    SampleRatePolicy_OnNewSession(policy);
}

void SampleRatePolicy_OnNewSession(SampleRatePolicy *policy)
{
    if (policy == NULL)
    {
        return;
    }
    policy->requested_sample_rate_Hz = SAMPLE_RATE_POLICY_DEFAULT_REQUEST_HZ;
    policy->clean_high_rate_frames = 0U;
    policy->reverted_in_session = false;
}

uint32_t SampleRatePolicy_Next(
    SampleRatePolicy *policy,
    uint32_t actual_sample_rate_Hz,
    const SignalResult *result)
{
    const bool has_interference = (result != NULL)
        && ((result->quality_flags & SIGNAL_QUALITY_INTERFERENCE_REMOVED) != 0U);
    const uint32_t recommended = (result != NULL)
        ? result->recommended_sample_rate_Hz
        : 0U;
    const bool valid_recommendation = (recommended >= H743_SET_FS_MIN_HZ)
        && (recommended <= H743_SET_FS_MAX_REQUEST_HZ);

    if (policy == NULL)
    {
        return SAMPLE_RATE_POLICY_DEFAULT_REQUEST_HZ;
    }
    if (result == NULL)
    {
        return policy->requested_sample_rate_Hz;
    }

    if (SampleRatePolicy_IsHighRate(actual_sample_rate_Hz))
    {
        const bool clean_result =
            (result->status == SIGNAL_STATUS_VALID) && !has_interference;

        if (!clean_result)
        {
            policy->clean_high_rate_frames = 0U;
            return policy->requested_sample_rate_Hz;
        }

        ++policy->clean_high_rate_frames;
        if (policy->clean_high_rate_frames >= SAMPLE_RATE_POLICY_CLEAN_FRAMES_TO_REVERT)
        {
            policy->clean_high_rate_frames = 0U;
            policy->reverted_in_session = true;
            policy->requested_sample_rate_Hz = SAMPLE_RATE_POLICY_DEFAULT_REQUEST_HZ;
        }
        return policy->requested_sample_rate_Hz;
    }

    policy->clean_high_rate_frames = 0U;
    if (valid_recommendation)
    {
        if (SampleRatePolicy_IsHighRate(recommended)
            && !SampleRatePolicy_CanStepUpToHighRate(policy, result))
        {
            policy->requested_sample_rate_Hz = SAMPLE_RATE_POLICY_DEFAULT_REQUEST_HZ;
        }
        else
        {
            policy->requested_sample_rate_Hz = recommended;
        }
    }
    return policy->requested_sample_rate_Hz;
}
