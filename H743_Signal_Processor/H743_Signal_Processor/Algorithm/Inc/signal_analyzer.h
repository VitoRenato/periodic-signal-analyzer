#ifndef SIGNAL_ANALYZER_H
#define SIGNAL_ANALYZER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SIGNAL_FRAME_SAMPLES   4096U
#define SIGNAL_MAX_COMPONENTS     3U

#define SIGNAL_QUALITY_LOWER_CLIP_DETECTED      0x01U
#define SIGNAL_QUALITY_CLIPPED_MODEL_RECOVERED  0x02U
#define SIGNAL_QUALITY_INTERFERENCE_REMOVED     0x04U

/**
 * @brief 参数估计算法的处理状态。
 *
 * 主机测试、ARM 编译和硬件实测必须分别记录；状态码只描述当前输入帧的
 * 算法结果，不代表物理通信链路已经验证。
 */
typedef enum
{
    SIGNAL_STATUS_VALID = 0,
    SIGNAL_STATUS_BAD_ARGUMENT,
    SIGNAL_STATUS_CLIPPED,
    SIGNAL_STATUS_NO_SIGNAL,
    SIGNAL_STATUS_MODEL_INVALID,
    SIGNAL_STATUS_ILL_CONDITIONED,
    SIGNAL_STATUS_SAMPLE_RATE_UNSAFE
} SignalStatus;

/**
 * @brief 单个正弦分量，幅度统一使用峰值。
 */
typedef struct
{
    float frequency_Hz;
    float amplitude_peak_V;
    float phase_rad;
} SignalComponent;

/**
 * @brief 一帧 4096 点信号的参数估计结果。
 */
typedef struct
{
    SignalStatus status;
    uint32_t sample_rate_Hz;
    float fundamental_Hz;
    float upp_V;
    float urms_V;
    float residual_rms_V;
    float snr_dB;
    uint8_t component_count;
    uint8_t quality_flags;
    SignalComponent component[SIGNAL_MAX_COMPONENTS];
    uint32_t recommended_sample_rate_Hz;
} SignalResult;

/**
 * @brief 处理一帧 AD9226 原始采样。
 *
 * @param samples         固定 4096 点原始码；有效数据位为 bit0-bit11。
 * @param sample_rate_Hz  采集端最近一次实际应用的采样率，单位 Hz。
 * @param result          调用方提供的结果存储区。
 *
 * @return 与 result->status 相同的状态码；result 为 NULL 时直接返回
 *         SIGNAL_STATUS_BAD_ARGUMENT。
 *
 * @note 当前实现复用模块内部静态工作区，只允许主循环中的单调用者串行调用，
 *       不可在中断、多个线程或嵌套调用中并发使用。
 */
SignalStatus SignalAnalyzer_Process(
    const uint16_t samples[SIGNAL_FRAME_SAMPLES],
    uint32_t sample_rate_Hz,
    SignalResult *result);

/* Fixed passive frontend: frequency-dependent amplitude correction is applied
 * after raw-domain model validation. Residual and SNR remain uncorrected.
 */
SignalStatus SignalAnalyzer_ProcessCalibrated(
    const uint16_t samples[SIGNAL_FRAME_SAMPLES],
    uint32_t sample_rate_Hz,
    SignalResult *result);

#ifdef __cplusplus
}
#endif

#endif
