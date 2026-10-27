#include "acq_frame_header.h"

#include "h743_crc32.h"

static uint16_t AcqFrameHeader_ReadU16Le(const uint8_t bytes[2])
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t AcqFrameHeader_ReadU32Le(const uint8_t bytes[4])
{
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8U)
        | ((uint32_t)bytes[2] << 16U)
        | ((uint32_t)bytes[3] << 24U);
}

static uint16_t AcqFrameHeader_Crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    size_t index;

    for (index = 0U; index < length; ++index)
    {
        uint8_t bit;

        crc ^= (uint16_t)((uint16_t)data[index] << 8U);
        for (bit = 0U; bit < 8U; ++bit)
        {
            crc = ((crc & 0x8000U) != 0U)
                ? (uint16_t)((uint16_t)(crc << 1U) ^ 0x1021U)
                : (uint16_t)(crc << 1U);
        }
    }
    return crc;
}

AcqFrameStatus AcqFrameHeader_Validate(
    const uint8_t frame[ACQ_FRAME_TOTAL_BYTES],
    AcqFrameHeader *out)
{
    uint16_t divider;
    uint32_t expected_sample_crc;
    uint32_t actual_sample_crc;

    if ((frame == NULL) || (out == NULL))
    {
        return ACQ_FRAME_ERR_NULL;
    }
    if (AcqFrameHeader_ReadU16Le(&frame[0U]) != ACQ_FRAME_MAGIC)
    {
        return ACQ_FRAME_ERR_MAGIC;
    }
    if (frame[2U] != ACQ_FRAME_VERSION)
    {
        return ACQ_FRAME_ERR_VERSION;
    }
    if (AcqFrameHeader_ReadU16Le(&frame[14U]) != AcqFrameHeader_Crc16(frame, 14U))
    {
        return ACQ_FRAME_ERR_HEADER_CRC;
    }

    divider = AcqFrameHeader_ReadU16Le(&frame[4U]);
    if ((divider < ACQ_FRAME_MIN_DIVIDER) || (divider > ACQ_FRAME_MAX_DIVIDER))
    {
        return ACQ_FRAME_ERR_DIVIDER;
    }

    expected_sample_crc = AcqFrameHeader_ReadU32Le(&frame[10U]);
    actual_sample_crc = H743Crc32_Compute(
        &frame[ACQ_FRAME_HEADER_BYTES],
        ACQ_FRAME_SAMPLE_BYTES);
    if (actual_sample_crc != expected_sample_crc)
    {
        return ACQ_FRAME_ERR_SAMPLE_CRC;
    }

    out->flags = frame[3U];
    out->timer_divider = divider;
    out->sample_rate_Hz = ACQ_FRAME_TIMER_CLOCK_HZ / (uint32_t)divider;
    out->sequence = AcqFrameHeader_ReadU32Le(&frame[6U]);
    return ACQ_FRAME_OK;
}
