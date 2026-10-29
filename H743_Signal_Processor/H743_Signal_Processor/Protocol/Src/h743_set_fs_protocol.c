#include "h743_set_fs_protocol.h"

#include <stddef.h>

#define H743_SET_FS_MAGIC 0xa55aU
#define H743_SET_FS_TIMER_CLOCK_HZ 168000000U

static void H743SetFsProtocol_WriteU16Le(uint8_t bytes[2], uint16_t value)
{
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xffU);
}

static void H743SetFsProtocol_WriteU32Le(uint8_t bytes[4], uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xffU);
    bytes[2] = (uint8_t)((value >> 16U) & 0xffU);
    bytes[3] = (uint8_t)((value >> 24U) & 0xffU);
}

static uint16_t H743SetFsProtocol_Crc16Update(
    uint16_t crc,
    const uint8_t *data,
    size_t length)
{
    size_t index;

    for (index = 0U; index < length; ++index)
    {
        uint8_t bit;

        crc ^= (uint16_t)((uint16_t)data[index] << 8U);
        for (bit = 0U; bit < 8U; ++bit)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((uint16_t)(crc << 1U) ^ 0x1021U);
            }
            else
            {
                crc = (uint16_t)(crc << 1U);
            }
        }
    }
    return crc;
}

static uint16_t H743SetFsProtocol_FrameCrc(const uint8_t *frame)
{
    uint16_t crc = H743SetFsProtocol_Crc16Update(0xffffU, frame, 8U);

    return H743SetFsProtocol_Crc16Update(
        crc,
        &frame[H743_SET_FS_REQUEST_GEN_OFFSET],
        12U);
}

bool H743SetFsProtocol_Encode(
    uint32_t requested_sample_rate_Hz,
    uint8_t frame[H743_SET_FS_FRAME_SIZE])
{
    return H743SetFsProtocol_EncodeWithMeasurements(
        requested_sample_rate_Hz,
        0U,
        0U,
        frame);
}

bool H743SetFsProtocol_EncodeWithMeasurements(
    uint32_t requested_sample_rate_Hz,
    uint32_t fundamental_hz,
    uint16_t vpp_mv,
    uint8_t frame[H743_SET_FS_FRAME_SIZE])
{
    uint8_t payload[6];

    H743SetFsProtocol_WriteU32Le(&payload[0], fundamental_hz);
    H743SetFsProtocol_WriteU16Le(&payload[4], vpp_mv);
    return H743SetFsProtocol_EncodePage(
        requested_sample_rate_Hz,
        0x00U,
        0U,
        0U,
        payload,
        frame);
}

bool H743SetFsProtocol_EncodePage(
    uint32_t requested_sample_rate_Hz,
    uint8_t flags,
    uint16_t request_generation,
    uint32_t analysis_sample_rate_Hz,
    const uint8_t payload[6],
    uint8_t frame[H743_SET_FS_FRAME_SIZE])
{
    uint8_t encoded[H743_SET_FS_FRAME_SIZE] = {0};
    uint16_t crc;
    size_t index;

    if ((frame == NULL)
        || (payload == NULL)
        || (requested_sample_rate_Hz < H743_SET_FS_MIN_HZ)
        || (requested_sample_rate_Hz > H743_SET_FS_MAX_REQUEST_HZ))
    {
        return false;
    }

    H743SetFsProtocol_WriteU16Le(&encoded[0], H743_SET_FS_MAGIC);
    encoded[2] = H743_SET_FS_TYPE;
    encoded[3] = flags;
    H743SetFsProtocol_WriteU32Le(&encoded[4], requested_sample_rate_Hz);
    H743SetFsProtocol_WriteU16Le(
        &encoded[H743_SET_FS_REQUEST_GEN_OFFSET],
        request_generation);
    H743SetFsProtocol_WriteU32Le(
        &encoded[H743_SET_FS_ANALYSIS_RATE_OFFSET],
        analysis_sample_rate_Hz);
    for (index = 0U; index < 6U; ++index)
    {
        encoded[H743_SET_FS_PAYLOAD_OFFSET + index] = payload[index];
    }
    crc = H743SetFsProtocol_FrameCrc(encoded);
    H743SetFsProtocol_WriteU16Le(&encoded[8], crc);

    for (index = 0U; index < H743_SET_FS_FRAME_SIZE; ++index)
    {
        frame[index] = encoded[index];
    }
    return true;
}

uint32_t H743SetFsProtocol_ActualRate(uint32_t requested_sample_rate_Hz)
{
    uint32_t divider;

    if ((requested_sample_rate_Hz < H743_SET_FS_MIN_HZ)
        || (requested_sample_rate_Hz > H743_SET_FS_MAX_REQUEST_HZ))
    {
        return 0U;
    }

    divider = H743_SET_FS_TIMER_CLOCK_HZ / requested_sample_rate_Hz;
    if (divider == 0U)
    {
        return 0U;
    }
    return H743_SET_FS_TIMER_CLOCK_HZ / divider;
}
