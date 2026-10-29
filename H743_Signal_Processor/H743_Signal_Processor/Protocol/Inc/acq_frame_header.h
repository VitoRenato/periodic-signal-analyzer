/**
 * @file acq_frame_header.h
 * @brief H743 端解码 F407 发来的 8208 B 样本帧：校验 16 B 帧头、样本 CRC，并算出采样率。
 *
 * 与 F407 端 USER/protocol/acq_frame_tx.h 成对，两边格式必须同步修改。
 */
#ifndef ACQ_FRAME_HEADER_H
#define ACQ_FRAME_HEADER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* v3: 帧头扩到 22 B；[16..17] 为本轮请求代号，[18..21] 保留零字节。 */
#define ACQ_FRAME_HEADER_BYTES       22U
#define ACQ_FRAME_SAMPLE_BYTES       8192U
#define ACQ_FRAME_TOTAL_BYTES        (ACQ_FRAME_HEADER_BYTES + ACQ_FRAME_SAMPLE_BYTES)
#define ACQ_FRAME_MAGIC              0x5AA5U
#define ACQ_FRAME_VERSION            3U
#define ACQ_FRAME_FLAG_BOOT          0x01U
#define ACQ_FRAME_FLAG_REARM         0x02U
#define ACQ_FRAME_TIMER_CLOCK_HZ     168000000U
/* 与 AD9226_FS_MAX=20 MHz 对应的最小分频 (168M / 8 = 21 MHz，168M / 9 = 18.67 MHz)。 */
#define ACQ_FRAME_MIN_DIVIDER        8U
/* 与 AD9226_FS_MIN=5 kHz 对应的最大分频 (168M / 33600 = 5000 Hz)。 */
#define ACQ_FRAME_MAX_DIVIDER        33600U

typedef enum
{
    ACQ_FRAME_OK = 0,
    ACQ_FRAME_ERR_NULL,
    ACQ_FRAME_ERR_MAGIC,
    ACQ_FRAME_ERR_VERSION,
    ACQ_FRAME_ERR_HEADER_CRC,
    ACQ_FRAME_ERR_DIVIDER,
    ACQ_FRAME_ERR_SAMPLE_CRC,
    ACQ_FRAME_ERR_RESERVED
} AcqFrameStatus;

typedef struct
{
    uint8_t flags;
    uint16_t timer_divider;
    uint32_t sample_rate_Hz;
    uint32_t sequence;
    uint16_t request_generation;
} AcqFrameHeader;

AcqFrameStatus AcqFrameHeader_Validate(
    const uint8_t frame[ACQ_FRAME_TOTAL_BYTES],
    AcqFrameHeader *out);

static inline bool AcqFrameHeader_IsNewSession(const AcqFrameHeader *header)
{
    return (header != NULL)
        && ((header->flags & (ACQ_FRAME_FLAG_BOOT | ACQ_FRAME_FLAG_REARM)) != 0U);
}

#ifdef __cplusplus
}
#endif

#endif
