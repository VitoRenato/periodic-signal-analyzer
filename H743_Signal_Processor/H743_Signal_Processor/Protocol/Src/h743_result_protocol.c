#include "h743_result_protocol.h"

#include "h743_crc32.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define H743_RESULT_MAGIC UINT16_C(0x4853)
#define H743_RESULT_VERSION UINT8_C(1)
#define H743_RESULT_MESSAGE_TYPE UINT8_C(0x21)
#define H743_RESULT_KNOWN_FLAGS_MASK UINT32_C(0x0000003f)
#define H743_RESULT_PHASE_MIN_MDEG (-180000)
#define H743_RESULT_PHASE_MAX_MDEG 180000
#define H743_RESULT_PI 3.14159265358979323846

#define H743_OFFSET_MAGIC 0U
#define H743_OFFSET_VERSION 2U
#define H743_OFFSET_MESSAGE_TYPE 3U
#define H743_OFFSET_TOTAL_LENGTH 4U
#define H743_OFFSET_HEADER_FLAGS 6U
#define H743_OFFSET_SEQUENCE 8U
#define H743_OFFSET_SAMPLE_RATE 12U
#define H743_OFFSET_STATUS 16U
#define H743_OFFSET_COMPONENT_COUNT 18U
#define H743_OFFSET_RESERVED0 19U
#define H743_OFFSET_FUNDAMENTAL 20U
#define H743_OFFSET_UPP 24U
#define H743_OFFSET_URMS 28U
#define H743_OFFSET_RESIDUAL_RMS 32U
#define H743_OFFSET_SNR 36U
#define H743_OFFSET_RECOMMENDED_FS 40U
#define H743_OFFSET_COMPONENTS 44U
#define H743_COMPONENT_SLOT_SIZE 12U
#define H743_OFFSET_RESULT_FLAGS 80U
#define H743_OFFSET_CRC32 84U

static void H743Protocol_WriteU16(uint8_t bytes[2], uint16_t value)
{
    bytes[0] = (uint8_t)(value & UINT16_C(0x00ff));
    bytes[1] = (uint8_t)((value >> 8U) & UINT16_C(0x00ff));
}

static void H743Protocol_WriteU32(uint8_t bytes[4], uint32_t value)
{
    bytes[0] = (uint8_t)(value & UINT32_C(0x000000ff));
    bytes[1] = (uint8_t)((value >> 8U) & UINT32_C(0x000000ff));
    bytes[2] = (uint8_t)((value >> 16U) & UINT32_C(0x000000ff));
    bytes[3] = (uint8_t)((value >> 24U) & UINT32_C(0x000000ff));
}

static void H743Protocol_WriteI32(uint8_t bytes[4], int32_t value)
{
    H743Protocol_WriteU32(bytes, (uint32_t)value);
}

static uint16_t H743Protocol_ReadU16(const uint8_t bytes[2])
{
    return (uint16_t)((uint16_t)bytes[0]
        | ((uint16_t)bytes[1] << 8U));
}

static uint32_t H743Protocol_ReadU32(const uint8_t bytes[4])
{
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8U)
        | ((uint32_t)bytes[2] << 16U)
        | ((uint32_t)bytes[3] << 24U);
}

static int32_t H743Protocol_ReadI32(const uint8_t bytes[4])
{
    const uint32_t value = H743Protocol_ReadU32(bytes);

    if (value <= (uint32_t)INT32_MAX)
    {
        return (int32_t)value;
    }
    return -(int32_t)(UINT32_MAX - value) - 1;
}

static bool H743Protocol_IsStatusValid(SignalStatus status)
{
    return (uint32_t)status <= (uint32_t)SIGNAL_STATUS_SAMPLE_RATE_UNSAFE;
}

static bool H743Protocol_ScaleFrequency(
    float frequency_Hz,
    bool require_positive,
    uint32_t *frequency_mHz)
{
    double scaled;

    if (!isfinite(frequency_Hz)
        || (frequency_Hz < 0.0f)
        || (require_positive && (frequency_Hz <= 0.0f)))
    {
        return false;
    }

    scaled = (double)frequency_Hz * 1000.0;
    if (!isfinite(scaled) || (scaled > (double)UINT32_MAX))
    {
        return false;
    }

    scaled = floor(scaled + 0.5);
    if (scaled > (double)UINT32_MAX)
    {
        return false;
    }
    *frequency_mHz = (uint32_t)scaled;
    return true;
}

static bool H743Protocol_ScaleUnsignedMetric(
    float value,
    double scale,
    uint32_t *encoded)
{
    double scaled;

    if (!isfinite(value) || (value < 0.0f))
    {
        return false;
    }

    scaled = (double)value * scale;
    if (!isfinite(scaled) || (scaled >= (double)UINT32_MAX))
    {
        *encoded = UINT32_MAX;
        return true;
    }

    *encoded = (uint32_t)floor(scaled + 0.5);
    return true;
}

static bool H743Protocol_ScaleSnr(float snr_dB, int32_t *snr_milli_dB)
{
    double scaled;

    if (!isfinite(snr_dB))
    {
        return false;
    }

    scaled = (double)snr_dB * 1000.0;
    if (scaled >= (double)INT32_MAX)
    {
        *snr_milli_dB = INT32_MAX;
    }
    else if (scaled <= (double)INT32_MIN)
    {
        *snr_milli_dB = INT32_MIN;
    }
    else if (scaled >= 0.0)
    {
        *snr_milli_dB = (int32_t)floor(scaled + 0.5);
    }
    else
    {
        *snr_milli_dB = (int32_t)ceil(scaled - 0.5);
    }
    return true;
}

static bool H743Protocol_ScalePhase(float phase_rad, int32_t *phase_mdeg)
{
    const double scaled = (double)phase_rad
        * 180000.0 / H743_RESULT_PI;
    double rounded;

    if (!isfinite(phase_rad) || !isfinite(scaled))
    {
        return false;
    }

    rounded = (scaled >= 0.0)
        ? floor(scaled + 0.5)
        : ceil(scaled - 0.5);
    if ((rounded < (double)H743_RESULT_PHASE_MIN_MDEG)
        || (rounded >= (double)H743_RESULT_PHASE_MAX_MDEG))
    {
        return false;
    }

    *phase_mdeg = (int32_t)rounded;
    return true;
}

static bool H743Protocol_IsZeroRange(
    const uint8_t *bytes,
    size_t length)
{
    size_t byte_index;

    for (byte_index = 0U; byte_index < length; ++byte_index)
    {
        if (bytes[byte_index] != 0U)
        {
            return false;
        }
    }
    return true;
}

bool H743ResultProtocol_Encode(
    const SignalResult *result,
    uint32_t sequence,
    uint32_t result_flags,
    uint8_t frame[H743_RESULT_FRAME_SIZE])
{
    uint8_t encoded[H743_RESULT_FRAME_SIZE] = {0U};
    uint32_t fundamental_mHz;
    uint32_t upp_uV;
    uint32_t urms_uV;
    uint32_t residual_rms_uV;
    int32_t snr_milli_dB;
    uint32_t component_frequency_mHz[SIGNAL_MAX_COMPONENTS] = {0U};
    uint32_t component_amplitude_uV[SIGNAL_MAX_COMPONENTS] = {0U};
    int32_t component_phase_mdeg[SIGNAL_MAX_COMPONENTS] = {0};
    size_t component_index;

    if ((result == NULL)
        || (frame == NULL)
        || !H743Protocol_IsStatusValid(result->status)
        || (result->component_count > SIGNAL_MAX_COMPONENTS)
        || ((result_flags & ~H743_RESULT_KNOWN_FLAGS_MASK) != 0U)
        || !H743Protocol_ScaleFrequency(
            result->fundamental_Hz,
            false,
            &fundamental_mHz)
        || !H743Protocol_ScaleUnsignedMetric(result->upp_V, 1000000.0, &upp_uV)
        || !H743Protocol_ScaleUnsignedMetric(result->urms_V, 1000000.0, &urms_uV)
        || !H743Protocol_ScaleUnsignedMetric(
            result->residual_rms_V,
            1000000.0,
            &residual_rms_uV)
        || !H743Protocol_ScaleSnr(result->snr_dB, &snr_milli_dB))
    {
        return false;
    }

    for (component_index = 0U;
         component_index < (size_t)result->component_count;
         ++component_index)
    {
        const SignalComponent *component = &result->component[component_index];

        if (!H743Protocol_ScaleFrequency(
                component->frequency_Hz,
                true,
                &component_frequency_mHz[component_index])
            || !H743Protocol_ScaleUnsignedMetric(
                component->amplitude_peak_V,
                1000000.0,
                &component_amplitude_uV[component_index])
            || !H743Protocol_ScalePhase(
                component->phase_rad,
                &component_phase_mdeg[component_index]))
        {
            return false;
        }
    }

    /* 固定头部和所有保留字段先由零初始化保证，再逐字段显式写入。 */
    H743Protocol_WriteU16(&encoded[H743_OFFSET_MAGIC], H743_RESULT_MAGIC);
    encoded[H743_OFFSET_VERSION] = H743_RESULT_VERSION;
    encoded[H743_OFFSET_MESSAGE_TYPE] = H743_RESULT_MESSAGE_TYPE;
    H743Protocol_WriteU16(
        &encoded[H743_OFFSET_TOTAL_LENGTH],
        (uint16_t)H743_RESULT_FRAME_SIZE);
    H743Protocol_WriteU32(&encoded[H743_OFFSET_SEQUENCE], sequence);
    H743Protocol_WriteU32(
        &encoded[H743_OFFSET_SAMPLE_RATE],
        result->sample_rate_Hz);
    H743Protocol_WriteU16(
        &encoded[H743_OFFSET_STATUS],
        (uint16_t)result->status);
    encoded[H743_OFFSET_COMPONENT_COUNT] = result->component_count;
    H743Protocol_WriteU32(
        &encoded[H743_OFFSET_FUNDAMENTAL],
        fundamental_mHz);
    H743Protocol_WriteU32(&encoded[H743_OFFSET_UPP], upp_uV);
    H743Protocol_WriteU32(&encoded[H743_OFFSET_URMS], urms_uV);
    H743Protocol_WriteU32(
        &encoded[H743_OFFSET_RESIDUAL_RMS],
        residual_rms_uV);
    H743Protocol_WriteI32(&encoded[H743_OFFSET_SNR], snr_milli_dB);
    H743Protocol_WriteU32(
        &encoded[H743_OFFSET_RECOMMENDED_FS],
        result->recommended_sample_rate_Hz);

    for (component_index = 0U;
         component_index < (size_t)result->component_count;
         ++component_index)
    {
        const size_t slot_offset = H743_OFFSET_COMPONENTS
            + (component_index * H743_COMPONENT_SLOT_SIZE);

        H743Protocol_WriteU32(
            &encoded[slot_offset],
            component_frequency_mHz[component_index]);
        H743Protocol_WriteU32(
            &encoded[slot_offset + 4U],
            component_amplitude_uV[component_index]);
        H743Protocol_WriteI32(
            &encoded[slot_offset + 8U],
            component_phase_mdeg[component_index]);
    }

    H743Protocol_WriteU32(&encoded[H743_OFFSET_RESULT_FLAGS], result_flags);
    H743Protocol_WriteU32(
        &encoded[H743_OFFSET_CRC32],
        H743Crc32_Compute(encoded, H743_OFFSET_CRC32));

    (void)memcpy(frame, encoded, sizeof(encoded));
    return true;
}

bool H743ResultProtocol_Decode(
    const uint8_t frame[H743_RESULT_FRAME_SIZE],
    SignalResult *result,
    uint32_t *sequence,
    uint32_t *result_flags)
{
    SignalResult decoded = {0};
    const uint16_t status_value = (frame != NULL)
        ? H743Protocol_ReadU16(&frame[H743_OFFSET_STATUS])
        : 0U;
    const uint8_t component_count = (frame != NULL)
        ? frame[H743_OFFSET_COMPONENT_COUNT]
        : 0U;
    const uint32_t decoded_flags = (frame != NULL)
        ? H743Protocol_ReadU32(&frame[H743_OFFSET_RESULT_FLAGS])
        : 0U;
    size_t component_index;

    if ((frame == NULL)
        || (result == NULL)
        || (sequence == NULL)
        || (result_flags == NULL)
        || (H743Protocol_ReadU16(&frame[H743_OFFSET_MAGIC])
            != H743_RESULT_MAGIC)
        || (frame[H743_OFFSET_VERSION] != H743_RESULT_VERSION)
        || (frame[H743_OFFSET_MESSAGE_TYPE] != H743_RESULT_MESSAGE_TYPE)
        || (H743Protocol_ReadU16(&frame[H743_OFFSET_TOTAL_LENGTH])
            != H743_RESULT_FRAME_SIZE)
        || (H743Protocol_ReadU16(&frame[H743_OFFSET_HEADER_FLAGS]) != 0U)
        || (frame[H743_OFFSET_RESERVED0] != 0U)
        || (component_count > SIGNAL_MAX_COMPONENTS)
        || (status_value > (uint16_t)SIGNAL_STATUS_SAMPLE_RATE_UNSAFE)
        || ((decoded_flags & ~H743_RESULT_KNOWN_FLAGS_MASK) != 0U)
        || (H743Protocol_ReadU32(&frame[H743_OFFSET_CRC32])
            != H743Crc32_Compute(frame, H743_OFFSET_CRC32)))
    {
        return false;
    }

    for (component_index = 0U;
         component_index < (size_t)component_count;
         ++component_index)
    {
        const size_t slot_offset = H743_OFFSET_COMPONENTS
            + (component_index * H743_COMPONENT_SLOT_SIZE);
        const uint32_t frequency_mHz =
            H743Protocol_ReadU32(&frame[slot_offset]);
        const int32_t phase_mdeg =
            H743Protocol_ReadI32(&frame[slot_offset + 8U]);

        if ((frequency_mHz == 0U)
            || (phase_mdeg < H743_RESULT_PHASE_MIN_MDEG)
            || (phase_mdeg >= H743_RESULT_PHASE_MAX_MDEG))
        {
            return false;
        }
    }

    /* component_count之后的槽必须全零，防止残留数据被误当成有效分量。 */
    for (component_index = (size_t)component_count;
         component_index < SIGNAL_MAX_COMPONENTS;
         ++component_index)
    {
        const size_t slot_offset = H743_OFFSET_COMPONENTS
            + (component_index * H743_COMPONENT_SLOT_SIZE);

        if (!H743Protocol_IsZeroRange(
                &frame[slot_offset],
                H743_COMPONENT_SLOT_SIZE))
        {
            return false;
        }
    }

    decoded.status = (SignalStatus)status_value;
    decoded.sample_rate_Hz = H743Protocol_ReadU32(
        &frame[H743_OFFSET_SAMPLE_RATE]);
    decoded.fundamental_Hz = (float)((double)H743Protocol_ReadU32(
        &frame[H743_OFFSET_FUNDAMENTAL]) / 1000.0);
    decoded.upp_V = (float)((double)H743Protocol_ReadU32(
        &frame[H743_OFFSET_UPP]) / 1000000.0);
    decoded.urms_V = (float)((double)H743Protocol_ReadU32(
        &frame[H743_OFFSET_URMS]) / 1000000.0);
    decoded.residual_rms_V = (float)((double)H743Protocol_ReadU32(
        &frame[H743_OFFSET_RESIDUAL_RMS]) / 1000000.0);
    decoded.snr_dB = (float)((double)H743Protocol_ReadI32(
        &frame[H743_OFFSET_SNR]) / 1000.0);
    decoded.component_count = component_count;
    decoded.recommended_sample_rate_Hz = H743Protocol_ReadU32(
        &frame[H743_OFFSET_RECOMMENDED_FS]);

    for (component_index = 0U;
         component_index < (size_t)component_count;
         ++component_index)
    {
        const size_t slot_offset = H743_OFFSET_COMPONENTS
            + (component_index * H743_COMPONENT_SLOT_SIZE);

        decoded.component[component_index].frequency_Hz =
            (float)((double)H743Protocol_ReadU32(&frame[slot_offset])
                / 1000.0);
        decoded.component[component_index].amplitude_peak_V =
            (float)((double)H743Protocol_ReadU32(&frame[slot_offset + 4U])
                / 1000000.0);
        decoded.component[component_index].phase_rad =
            (float)((double)H743Protocol_ReadI32(&frame[slot_offset + 8U])
                * H743_RESULT_PI / 180000.0);
    }

    *result = decoded;
    *sequence = H743Protocol_ReadU32(&frame[H743_OFFSET_SEQUENCE]);
    *result_flags = decoded_flags;
    return true;
}