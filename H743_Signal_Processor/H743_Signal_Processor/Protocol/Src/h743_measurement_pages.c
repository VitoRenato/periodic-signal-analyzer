#include "h743_measurement_pages.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>

#define H743_MEASUREMENT_PI 3.14159265358979323846
#define H743_MEASUREMENT_MAX_HARMONIC_ORDER 50U
#define H743_MEASUREMENT_HARMONIC_ERROR_HZ 1000.0

static void H743Measurement_WriteU16Le(uint8_t bytes[2], uint16_t value)
{
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xffU);
}

static void H743Measurement_WriteU32Le(uint8_t bytes[4], uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xffU);
    bytes[2] = (uint8_t)((value >> 16U) & 0xffU);
    bytes[3] = (uint8_t)((value >> 24U) & 0xffU);
}

static uint32_t H743Measurement_QuantizeU32(double value, double scale)
{
    const double scaled = value * scale;

    if (!isfinite(scaled) || (scaled <= 0.0))
    {
        return 0U;
    }
    if (scaled >= (double)UINT32_MAX)
    {
        return UINT32_MAX;
    }
    return (uint32_t)floor(scaled + 0.5);
}

static uint16_t H743Measurement_QuantizeU16(double value, double scale)
{
    const uint32_t scaled = H743Measurement_QuantizeU32(value, scale);

    return (scaled > (uint32_t)UINT16_MAX)
        ? UINT16_MAX
        : (uint16_t)scaled;
}

static int16_t H743Measurement_QuantizePhase(float phase_rad)
{
    double scaled = (double)phase_rad * 32768.0 / H743_MEASUREMENT_PI;

    if (!isfinite(scaled))
    {
        return 0;
    }
    while (scaled >= 32768.0)
    {
        scaled -= 65536.0;
    }
    while (scaled < -32768.0)
    {
        scaled += 65536.0;
    }
    scaled = (scaled >= 0.0)
        ? floor(scaled + 0.5)
        : ceil(scaled - 0.5);
    if (scaled > (double)INT16_MAX)
    {
        return INT16_MAX;
    }
    if (scaled < (double)INT16_MIN)
    {
        return INT16_MIN;
    }
    return (int16_t)scaled;
}

static int H743Measurement_GetOrder(
    const SignalResult *result,
    size_t component_index,
    uint8_t *order)
{
    const double fundamental_Hz = (double)result->fundamental_Hz;
    const double frequency_Hz =
        (double)result->component[component_index].frequency_Hz;
    const double ratio = frequency_Hz / fundamental_Hz;
    const double rounded = floor(ratio + 0.5);

    if (!isfinite(ratio)
        || (rounded < 1.0)
        || (rounded > (double)H743_MEASUREMENT_MAX_HARMONIC_ORDER)
        || (fabs(frequency_Hz - (rounded * fundamental_Hz))
            > H743_MEASUREMENT_HARMONIC_ERROR_HZ))
    {
        return 0;
    }
    *order = (uint8_t)rounded;
    return 1;
}

static int H743Measurement_ValidateResult(const SignalResult *result)
{
    size_t component_index;

    if ((result == NULL)
        || ((uint32_t)result->status
            > (uint32_t)SIGNAL_STATUS_SAMPLE_RATE_UNSAFE)
        || (result->component_count > SIGNAL_MAX_COMPONENTS)
        || !isfinite(result->fundamental_Hz)
        || !isfinite(result->upp_V)
        || !isfinite(result->urms_V)
        || (result->fundamental_Hz < 0.0f)
        || (result->upp_V < 0.0f)
        || (result->urms_V < 0.0f)
        || ((result->component_count != 0U)
            && (result->fundamental_Hz <= 0.0f)))
    {
        return 0;
    }

    for (component_index = 0U;
         component_index < (size_t)result->component_count;
         ++component_index)
    {
        uint8_t order;
        const SignalComponent *component = &result->component[component_index];

        if (!isfinite(component->amplitude_peak_V)
            || !isfinite(component->phase_rad)
            || (component->amplitude_peak_V < 0.0f)
            || !H743Measurement_GetOrder(result, component_index, &order))
        {
            return 0;
        }
    }
    return 1;
}

static void H743Measurement_ActivatePending(H743MeasurementPager *pager)
{
    pager->active_result = pager->pending_result;
    pager->active_sample_rate_Hz = pager->pending_sample_rate_Hz;
    pager->pending_valid = 0U;
    pager->active_valid = 1U;
    pager->model_id = (uint8_t)((pager->model_id + 1U) & 0x07U);
}

void H743MeasurementPager_Init(H743MeasurementPager *pager)
{
    if (pager != NULL)
    {
        *pager = (H743MeasurementPager){0};
        pager->model_id = 7U;
    }
}

bool H743MeasurementPager_Load(
    H743MeasurementPager *pager,
    const SignalResult *result,
    uint32_t requested_sample_rate_Hz)
{
    if ((pager == NULL)
        || !H743Measurement_ValidateResult(result)
        || (requested_sample_rate_Hz < H743_SET_FS_MIN_HZ)
        || (requested_sample_rate_Hz > H743_SET_FS_MAX_REQUEST_HZ))
    {
        return false;
    }

    pager->pending_result = *result;
    pager->pending_sample_rate_Hz = requested_sample_rate_Hz;
    pager->pending_valid = 1U;
    if (pager->active_valid == 0U)
    {
        H743Measurement_ActivatePending(pager);
    }
    return true;
}

bool H743MeasurementPager_EncodeNext(
    H743MeasurementPager *pager,
    uint8_t frame[H743_SET_FS_FRAME_SIZE])
{
    uint8_t payload[6] = {0U};
    uint8_t flags;
    const uint8_t page = (pager != NULL) ? pager->page : 0U;

    if ((pager == NULL) || (frame == NULL) || (pager->active_valid == 0U))
    {
        return false;
    }
    if ((page == 0U) && (pager->pending_valid != 0U))
    {
        H743Measurement_ActivatePending(pager);
    }

    if (page == 0U)
    {
        H743Measurement_WriteU32Le(
            &payload[0U],
            H743Measurement_QuantizeU32(
                (double)pager->active_result.fundamental_Hz,
                1.0));
        H743Measurement_WriteU16Le(
            &payload[4U],
            H743Measurement_QuantizeU16(
                (double)pager->active_result.upp_V,
                1000.0));
    }
    else if (page == 1U)
    {
        H743Measurement_WriteU16Le(
            &payload[0U],
            H743Measurement_QuantizeU16(
                (double)pager->active_result.urms_V,
                1000.0));
        payload[2U] = pager->active_result.component_count;
        payload[3U] = (uint8_t)pager->active_result.status;
        payload[4U] = pager->active_result.quality_flags;
    }
    else
    {
        const size_t component_index = (size_t)(page - 2U);

        if (component_index
            < (size_t)pager->active_result.component_count)
        {
            uint8_t order;
            const SignalComponent *component =
                &pager->active_result.component[component_index];
            const int16_t phase_q15 = H743Measurement_QuantizePhase(
                component->phase_rad);

            if (!H743Measurement_GetOrder(
                    &pager->active_result,
                    component_index,
                    &order))
            {
                return false;
            }
            payload[0U] = order;
            H743Measurement_WriteU16Le(
                &payload[1U],
                H743Measurement_QuantizeU16(
                    (double)component->amplitude_peak_V,
                    1000.0));
            H743Measurement_WriteU16Le(
                &payload[3U],
                (uint16_t)phase_q15);
            payload[5U] = 1U;
        }
    }

    flags = H743_MEASUREMENT_FLAG_VALID
        | (uint8_t)(page << H743_MEASUREMENT_PAGE_SHIFT)
        | (uint8_t)(pager->model_id << H743_MEASUREMENT_MODEL_SHIFT);
    if ((pager->active_result.quality_flags
            & SIGNAL_QUALITY_CLIPPED_MODEL_RECOVERED) != 0U)
    {
        flags |= H743_MEASUREMENT_FLAG_RECOVERED;
    }

    if (!H743SetFsProtocol_EncodePage(
            pager->active_sample_rate_Hz,
            flags,
            payload,
            frame))
    {
        return false;
    }
    pager->page = (uint8_t)((page + 1U) % H743_MEASUREMENT_PAGE_COUNT);
    return true;
}

void H743MeasurementPager_Rewind(H743MeasurementPager *pager)
{
    if (pager != NULL)
    {
        pager->page = 0U;
    }
}
