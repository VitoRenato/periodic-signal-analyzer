#ifndef H743_RESULT_PROTOCOL_H
#define H743_RESULT_PROTOCOL_H

#include "signal_analyzer.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define H743_RESULT_FRAME_SIZE 88U

/**
 * @brief 将算法结果编码为固定88字节RESULT版本1帧。
 *
 * 所有多字节字段均显式按小端写入，不直接发送C结构体。编码失败时frame保持
 * 不变。result_flags的bit6~bit31为保留位，必须为0。
 */
bool H743ResultProtocol_Encode(
    const SignalResult *result,
    uint32_t sequence,
    uint32_t result_flags,
    uint8_t frame[H743_RESULT_FRAME_SIZE]);

/**
 * @brief 校验并解码固定88字节RESULT版本1帧。
 *
 * 只有固定字段、保留位、槽位、相位范围和CRC全部通过后才更新输出参数；
 * 解码失败时result、sequence和result_flags保持不变。
 */
bool H743ResultProtocol_Decode(
    const uint8_t frame[H743_RESULT_FRAME_SIZE],
    SignalResult *result,
    uint32_t *sequence,
    uint32_t *result_flags);

#ifdef __cplusplus
}
#endif

#endif