/**
 * @file acq_frame_tx.h
 * @brief F407 -> H743 样本帧的 16 B 帧头编码（纯逻辑，不依赖 HAL）。
 *
 * 帧头放在 SPI1 全双工帧的前 16 B（MOSI 方向），小端：
 *   [0..1]   magic 0x5AA5
 *   [2]      版本号
 *   [3]      flags：bit0 上电后第一帧，bit1 新一轮测量的第一帧
 *   [4..5]   本帧采集时的 TIM1 分频值 (ARR+1)，Fs = 168 MHz / 分频值
 *   [6..9]   帧序号
 *   [10..13] 8192 B 样本的 CRC-32/ISO-HDLC
 *   [14..15] 字节 0..13 的 CRC16/CCITT-FALSE
 * H743 端解码见 Protocol/Inc/acq_frame_header.h，两边格式必须同步修改。
 */
#ifndef ACQ_FRAME_TX_H
#define ACQ_FRAME_TX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ACQ_FRAME_HEADER_BYTES   16U
#define ACQ_FRAME_MAGIC          0x5AA5U
#define ACQ_FRAME_VERSION        1U
#define ACQ_FRAME_FLAG_BOOT      0x01U
#define ACQ_FRAME_FLAG_REARM     0x02U

typedef struct
{
  uint8_t flags;
  uint16_t timer_divider;
  uint32_t sequence;
} AcqFrameTxInfo;

uint32_t AcqFrameTx_Crc32(const uint8_t *data, size_t length);
uint16_t AcqFrameTx_Crc16(const uint8_t *data, size_t length);

bool AcqFrameTx_Encode(const AcqFrameTxInfo *info,
                       const uint8_t *sample_bytes,
                       size_t sample_length,
                       uint8_t header[ACQ_FRAME_HEADER_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* ACQ_FRAME_TX_H */
