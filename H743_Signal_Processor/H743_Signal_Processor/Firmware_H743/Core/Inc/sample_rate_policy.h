/**
 * @file sample_rate_policy.h
 * @brief H743 自动采样率选择：升频避开干扰、连续无干扰后回退、抑制同轮测量内来回跳变。
 */
#ifndef SAMPLE_RATE_POLICY_H
#define SAMPLE_RATE_POLICY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "signal_analyzer.h"

#include <stdbool.h>
#include <stdint.h>

#define SAMPLE_RATE_POLICY_DEFAULT_REQUEST_HZ       2048780U
#define SAMPLE_RATE_POLICY_HIGH_REQUEST_HZ          10000000U
/* 10 MHz 下 4096 点约 410 us；低于 50 kHz 时单帧不足 ~20 个周期，不升到 10 MHz。 */
#define SAMPLE_RATE_POLICY_MIN_F0_FOR_HIGH_FS_HZ    50000.0f
/* 在高采样率下连续多少帧无干扰后回退到默认采样率。 */
#define SAMPLE_RATE_POLICY_CLEAN_FRAMES_TO_REVERT   3U

typedef struct
{
    uint32_t requested_sample_rate_Hz;
    uint8_t clean_high_rate_frames;
    /* 同轮测量内一旦从高采样率回退过，不再重新升频，防止 2 MHz <-> 10 MHz 振荡。 */
    bool reverted_in_session;
} SampleRatePolicy;

void SampleRatePolicy_Init(SampleRatePolicy *policy);

/* F407 帧头带 BOOT 或 REARM 时调用：恢复默认采样率并允许本轮升频一次。 */
void SampleRatePolicy_OnNewSession(SampleRatePolicy *policy);

/**
 * @param actual_sample_rate_Hz 本帧实际采样率（来自帧头）
 * @return 下一次写入 SET_FS 命令的请求采样率
 */
uint32_t SampleRatePolicy_Next(
    SampleRatePolicy *policy,
    uint32_t actual_sample_rate_Hz,
    const SignalResult *result);

#ifdef __cplusplus
}
#endif

#endif
