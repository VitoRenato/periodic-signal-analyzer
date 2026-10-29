#include "h743_measurement_rx.h"

#include <stddef.h>
#include <string.h>

#define H743_MEASUREMENT_MAGIC 0xa55aU
#define H743_MEASUREMENT_TYPE 0x81U
#define H743_MEASUREMENT_FLAG_VALID 0x01U
#define H743_MEASUREMENT_FLAG_RECOVERED 0x02U
#define H743_MEASUREMENT_PAGE_SHIFT 2U
#define H743_MEASUREMENT_PAGE_MASK 0x07U
#define H743_MEASUREMENT_MODEL_SHIFT 5U
#define H743_MEASUREMENT_MODEL_MASK 0x03U
#define H743_MEASUREMENT_FLAG_SESSION_START 0x80U
#define H743_MEASUREMENT_PAGE_COUNT 5U
#define H743_MEASUREMENT_MAX_STATUS 6U
#define H743_MEASUREMENT_ANALYSIS_RATE_OFFSET 12U
#define H743_MEASUREMENT_REQUEST_GEN_OFFSET 10U
#define H743_MEASUREMENT_PAYLOAD_OFFSET 16U

static uint16_t H743MeasurementRx_ReadU16Le(const uint8_t bytes[2])
{
    return (uint16_t)((uint16_t)bytes[0]
        | ((uint16_t)bytes[1] << 8U));
}

static uint32_t H743MeasurementRx_ReadU32Le(const uint8_t bytes[4])
{
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8U)
        | ((uint32_t)bytes[2] << 16U)
        | ((uint32_t)bytes[3] << 24U);
}

static uint16_t H743MeasurementRx_Crc16Update(
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
            crc = ((crc & 0x8000U) != 0U)
                ? (uint16_t)((uint16_t)(crc << 1U) ^ 0x1021U)
                : (uint16_t)(crc << 1U);
        }
    }
    return crc;
}

static uint16_t H743MeasurementRx_FrameCrc(const uint8_t *frame)
{
    const uint16_t crc = H743MeasurementRx_Crc16Update(0xffffU, frame, 8U);

    return H743MeasurementRx_Crc16Update(
        crc,
        &frame[H743_MEASUREMENT_REQUEST_GEN_OFFSET],
        12U);
}

static void H743MeasurementRx_ResetStaging(H743MeasurementRx *receiver)
{
    receiver->staging_model = (H743MeasurementModel){0};
    receiver->staging_active = 0U;
    receiver->active_model_id = 0U;
    receiver->expected_page = 0U;
    receiver->recovered = 0U;
    receiver->seen_pages = 0U;
    receiver->active_analysis_rate_Hz = 0U;
    receiver->active_request_generation = 0U;
}

static int H743MeasurementRx_IsZeroPayload(const uint8_t payload[6])
{
    size_t index;

    for (index = 0U; index < 6U; ++index)
    {
        if (payload[index] != 0U)
        {
            return 0;
        }
    }
    return 1;
}

static int H743MeasurementRx_DecodePage(
    H743MeasurementRx *receiver,
    uint8_t page,
    const uint8_t payload[6])
{
    H743MeasurementModel *model = &receiver->staging_model;

    if (page == 0U)
    {
        model->fundamental_Hz = H743MeasurementRx_ReadU32Le(&payload[0U]);
        model->upp_uV = (uint32_t)H743MeasurementRx_ReadU16Le(&payload[4U])
            * 1000U;
    }
    else if (page == 1U)
    {
        model->urms_uV = (uint32_t)H743MeasurementRx_ReadU16Le(&payload[0U])
            * 1000U;
        model->component_count = payload[2U];
        model->status = payload[3U];
        model->quality_flags = payload[4U];
        if ((payload[5U] != 0U)
            || (model->component_count > H743_MEASUREMENT_MAX_COMPONENTS)
            || (model->status > H743_MEASUREMENT_MAX_STATUS))
        {
            return 0;
        }
    }
    else
    {
        const size_t component_index = (size_t)(page - 2U);
        H743MeasurementComponent *component =
            &model->component[component_index];

        if (payload[5U] == 0U)
        {
            if (!H743MeasurementRx_IsZeroPayload(payload))
            {
                return 0;
            }
        }
        else if (payload[5U] == 1U)
        {
            component->order = payload[0U];
            component->amplitude_peak_uV =
                (uint32_t)H743MeasurementRx_ReadU16Le(&payload[1U])
                * 1000U;
            component->phase_q15 = (int16_t)H743MeasurementRx_ReadU16Le(
                &payload[3U]);
            component->valid = 1U;
            if (component->order == 0U)
            {
                return 0;
            }
        }
        else
        {
            return 0;
        }
    }
    return 1;
}

static int H743MeasurementRx_ValidateComplete(
    const H743MeasurementModel *model)
{
    size_t component_index;

    if ((model->component_count != 0U)
        && (model->fundamental_Hz == 0U))
    {
        return 0;
    }
    for (component_index = 0U;
         component_index < H743_MEASUREMENT_MAX_COMPONENTS;
         ++component_index)
    {
        const H743MeasurementComponent *component =
            &model->component[component_index];
        size_t previous_index;

        if (component_index < (size_t)model->component_count)
        {
            if ((component->valid == 0U) || (component->order == 0U))
            {
                return 0;
            }
            if ((component_index == 0U) && (component->order != 1U))
            {
                return 0;
            }
            for (previous_index = 0U;
                 previous_index < component_index;
                 ++previous_index)
            {
                if (model->component[previous_index].order
                    == component->order)
                {
                    return 0;
                }
            }
        }
        else if (component->valid != 0U)
        {
            return 0;
        }
    }
    return 1;
}

void H743MeasurementRx_Init(H743MeasurementRx *receiver)
{
    if (receiver != NULL)
    {
        *receiver = (H743MeasurementRx){0};
    }
}

bool H743MeasurementRx_ProcessFrame(
    H743MeasurementRx *receiver,
    const uint8_t *frame,
    uint32_t length)
{
    uint8_t flags;
    uint8_t page;
    uint8_t model_id;
    uint8_t recovered;
    uint32_t analysis_rate_Hz;
    uint16_t request_generation;
    bool session_start;

    if ((receiver == NULL)
        || (frame == NULL)
        || (length < H743_MEASUREMENT_FRAME_SIZE)
        || (H743MeasurementRx_ReadU16Le(&frame[0U])
            != H743_MEASUREMENT_MAGIC)
        || (frame[2U] != H743_MEASUREMENT_TYPE)
        || (H743MeasurementRx_ReadU16Le(&frame[8U])
            != H743MeasurementRx_FrameCrc(frame)))
    {
        if (receiver != NULL)
        {
            H743MeasurementRx_ResetStaging(receiver);
        }
        return false;
    }

    flags = frame[3U];
    page = (uint8_t)((flags >> H743_MEASUREMENT_PAGE_SHIFT)
        & H743_MEASUREMENT_PAGE_MASK);
    model_id = (uint8_t)((flags >> H743_MEASUREMENT_MODEL_SHIFT)
        & H743_MEASUREMENT_MODEL_MASK);
    recovered = (uint8_t)((flags & H743_MEASUREMENT_FLAG_RECOVERED) != 0U);
    session_start =
        ((flags & H743_MEASUREMENT_FLAG_SESSION_START) != 0U);
    analysis_rate_Hz =
        H743MeasurementRx_ReadU32Le(&frame[H743_MEASUREMENT_ANALYSIS_RATE_OFFSET]);
    request_generation = H743MeasurementRx_ReadU16Le(
        &frame[H743_MEASUREMENT_REQUEST_GEN_OFFSET]);
    if (((flags & H743_MEASUREMENT_FLAG_VALID) == 0U)
        || (page >= H743_MEASUREMENT_PAGE_COUNT)
        || (analysis_rate_Hz == 0U)
        || (request_generation != receiver->expected_request_generation))
    {
        H743MeasurementRx_ResetStaging(receiver);
        return false;
    }

    if (page == 0U)
    {
        H743MeasurementRx_ResetStaging(receiver);
        if (session_start)
        {
            receiver->session_bound = 1U;
            receiver->session_start_event = 1U;
        }
        if (receiver->session_bound == 0U)
        {
            return false;
        }
        receiver->staging_active = 1U;
        receiver->active_model_id = model_id;
        receiver->recovered = recovered;
        receiver->active_analysis_rate_Hz = analysis_rate_Hz;
        receiver->active_request_generation = request_generation;
    }
    else if ((receiver->session_bound == 0U)
        || (receiver->staging_active == 0U)
        || (receiver->active_model_id != model_id)
        || (receiver->recovered != recovered)
        || (receiver->active_analysis_rate_Hz != analysis_rate_Hz)
        || (receiver->active_request_generation != request_generation)
        || (receiver->expected_page != page))
    {
        H743MeasurementRx_ResetStaging(receiver);
        return false;
    }

    if (!H743MeasurementRx_DecodePage(
            receiver,
            page,
            &frame[H743_MEASUREMENT_PAYLOAD_OFFSET]))
    {
        H743MeasurementRx_ResetStaging(receiver);
        return false;
    }
    receiver->seen_pages |= (uint8_t)(1U << page);
    receiver->expected_page = (uint8_t)(page + 1U);

    if (page != (H743_MEASUREMENT_PAGE_COUNT - 1U))
    {
        return false;
    }
    if ((receiver->seen_pages != 0x1fU)
        || !H743MeasurementRx_ValidateComplete(&receiver->staging_model))
    {
        H743MeasurementRx_ResetStaging(receiver);
        return false;
    }

    ++receiver->published_sequence;
    receiver->staging_model.recovered = receiver->recovered;
    receiver->staging_model.model_id = receiver->active_model_id;
    receiver->staging_model.analysis_sample_rate_Hz =
        receiver->active_analysis_rate_Hz;
    receiver->staging_model.request_generation =
        receiver->active_request_generation;
    receiver->staging_model.published_sequence =
        receiver->published_sequence;
    receiver->published_model = receiver->staging_model;
    receiver->published_valid = 1U;
    H743MeasurementRx_ResetStaging(receiver);
    return true;
}

bool H743MeasurementRx_GetPublished(
    const H743MeasurementRx *receiver,
    H743MeasurementModel *model)
{
    if ((receiver == NULL)
        || (model == NULL)
        || (receiver->published_valid == 0U))
    {
        return false;
    }
    *model = receiver->published_model;
    return true;
}

bool H743MeasurementRx_TakeSessionStart(H743MeasurementRx *receiver)
{
    bool event;

    if (receiver == NULL)
    {
        return false;
    }
    event = (receiver->session_start_event != 0U);
    receiver->session_start_event = 0U;
    return event;
}

void H743MeasurementRx_SetRequestGeneration(
    H743MeasurementRx *receiver,
    uint16_t request_generation)
{
    if (receiver != NULL)
    {
        receiver->expected_request_generation = request_generation;
        receiver->session_bound = 0U;
        receiver->session_start_event = 0U;
        H743MeasurementRx_ResetStaging(receiver);
    }
}
