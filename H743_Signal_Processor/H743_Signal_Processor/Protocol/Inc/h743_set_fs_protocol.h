#ifndef H743_SET_FS_PROTOCOL_H
#define H743_SET_FS_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 命令槽总长 16 字节：前 10 字节为 SET_FS 命令，后 6 字节可捎带测量值 */
#define H743_SET_FS_FRAME_SIZE 16U
#define H743_SET_FS_MIN_HZ 5000U
#define H743_SET_FS_MAX_REQUEST_HZ 20000000U

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
    const uint8_t payload[6],
    uint8_t frame[H743_SET_FS_FRAME_SIZE]);

uint32_t H743SetFsProtocol_ActualRate(uint32_t requested_sample_rate_Hz);

#ifdef __cplusplus
}
#endif

#endif
