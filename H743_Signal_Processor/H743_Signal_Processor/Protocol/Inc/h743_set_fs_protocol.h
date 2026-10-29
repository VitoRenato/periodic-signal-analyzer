#ifndef H743_SET_FS_PROTOCOL_H
#define H743_SET_FS_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 命令槽总长 22 字节（v3）：
 *   [0..1] magic 0xA55A, [2] type 0x81, [3] flags,
 *   [4..7] 请求采样率, [8..9] CRC16,
 *   [10..11] 本轮请求代号（F407 样本帧头回显），
 *   [12..15] 本模型实际分析采样率, [16..21] 测量页载荷。
 * CRC 覆盖 [0..7] 与 [10..21]；旧 16/20 字节格式不再接受。
 */
#define H743_SET_FS_FRAME_SIZE 22U
#define H743_SET_FS_MIN_HZ 5000U
#define H743_SET_FS_MAX_REQUEST_HZ 20000000U
#define H743_SET_FS_TYPE 0x81U
#define H743_SET_FS_FLAG_SESSION_START 0x80U
#define H743_SET_FS_PAGE_SHIFT 2U
#define H743_SET_FS_PAGE_MASK 0x07U
#define H743_SET_FS_MODEL_SHIFT 5U
#define H743_SET_FS_MODEL_MASK 0x03U
#define H743_SET_FS_REQUEST_GEN_OFFSET 10U
#define H743_SET_FS_ANALYSIS_RATE_OFFSET 12U
#define H743_SET_FS_PAYLOAD_OFFSET 16U

bool H743SetFsProtocol_Encode(
    uint32_t requested_sample_rate_Hz,
    uint8_t frame[H743_SET_FS_FRAME_SIZE]);

/**
 * @brief 编码带测量值的 SET_FS 命令槽
 * @param requested_sample_rate_Hz  F4 下一帧目标采样率
 * @param fundamental_hz            H7 测得的基频(Hz)
 * @param vpp_mv                    H7 测得的峰峰值(mV)
 * @param frame                     输出 16 字节命令槽
 * @return true=编码成功
 * @note  flags bit0=1 表示后 6 字节测量值有效
 */
bool H743SetFsProtocol_EncodeWithMeasurements(
    uint32_t requested_sample_rate_Hz,
    uint32_t fundamental_hz,
    uint16_t vpp_mv,
    uint8_t frame[H743_SET_FS_FRAME_SIZE]);

/**
 * @brief 编码通用的 6 字节分页负载。
 *
 * CRC 覆盖 magic、type、flags 和采样率；失败时 frame 保持不变。
 */
bool H743SetFsProtocol_EncodePage(
    uint32_t requested_sample_rate_Hz,
    uint8_t flags,
    uint16_t request_generation,
    uint32_t analysis_sample_rate_Hz,
    const uint8_t payload[6],
    uint8_t frame[H743_SET_FS_FRAME_SIZE]);

uint32_t H743SetFsProtocol_ActualRate(uint32_t requested_sample_rate_Hz);

#ifdef __cplusplus
}
#endif

#endif
