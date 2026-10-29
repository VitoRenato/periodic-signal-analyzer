#include "h743_measurement_rx.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned int g_failures;

#define CHECK_TRUE(condition)                                                   \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            (void)fprintf(stderr, "FAIL %s:%d: %s\n",                        \
                __FILE__, __LINE__, #condition);                                \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

#define CHECK_U32(expected, actual)                                             \
    do                                                                          \
    {                                                                           \
        const uint32_t e_ = (uint32_t)(expected);                               \
        const uint32_t a_ = (uint32_t)(actual);                                 \
        if (e_ != a_)                                                           \
        {                                                                       \
            (void)fprintf(stderr,                                               \
                "FAIL %s:%d: expected %lu, got %lu\n",                       \
                __FILE__, __LINE__, (unsigned long)e_, (unsigned long)a_);      \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

static uint16_t crc16_update(uint16_t crc, const uint8_t *data, size_t length)
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

static void write_u16_le(uint8_t bytes[2], uint16_t value)
{
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xffU);
}

static void write_u32_le(uint8_t bytes[4], uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xffU);
    bytes[2] = (uint8_t)((value >> 16U) & 0xffU);
    bytes[3] = (uint8_t)((value >> 24U) & 0xffU);
}

static void seal(uint8_t frame[H743_MEASUREMENT_FRAME_SIZE])
{
    const uint16_t crc = crc16_update(
        crc16_update(0xffffU, frame, 8U), &frame[10U], 12U);

    write_u16_le(&frame[8U], crc);
}

static void make_page(
    uint8_t page,
    uint8_t model_id,
    bool session_start,
    uint32_t analysis_rate_Hz,
    const uint8_t payload[6],
    uint8_t frame[H743_MEASUREMENT_FRAME_SIZE])
{
    (void)memset(frame, 0, H743_MEASUREMENT_FRAME_SIZE);
    frame[0U] = 0x5aU;
    frame[1U] = 0xa5U;
    frame[2U] = 0x81U;
    frame[3U] = (uint8_t)(0x01U | (uint8_t)(page << 2U)
        | (uint8_t)((model_id & 0x03U) << 5U)
        | (session_start ? 0x80U : 0x00U));
    write_u32_le(&frame[4U], 2048780U);
    write_u16_le(&frame[10U], 1U);
    write_u32_le(&frame[12U], analysis_rate_Hz);
    (void)memcpy(&frame[16U], payload, 6U);
    seal(frame);
}

static void make_valid_model_pages(
    uint8_t model_id,
    uint32_t analysis_rate_Hz,
    uint8_t first_flags,
    uint8_t frame[5][H743_MEASUREMENT_FRAME_SIZE])
{
    uint8_t payload[6] = {0U};

    write_u32_le(&payload[0U], 100000U);
    write_u16_le(&payload[4U], 250U);
    make_page(0U, model_id, (first_flags & 0x80U) != 0U,
        analysis_rate_Hz, payload, frame[0U]);

    (void)memset(payload, 0, sizeof(payload));
    write_u16_le(&payload[0U], 88U);
    payload[2U] = 3U;
    payload[3U] = 0U;
    payload[4U] = 3U;
    make_page(1U, model_id, false, analysis_rate_Hz, payload, frame[1U]);

    (void)memset(payload, 0, sizeof(payload));
    payload[0U] = 1U;
    write_u16_le(&payload[1U], 100U);
    write_u16_le(&payload[3U], 0U);
    payload[5U] = 1U;
    make_page(2U, model_id, false, analysis_rate_Hz, payload, frame[2U]);

    payload[0U] = 3U;
    write_u16_le(&payload[1U], 30U);
    write_u16_le(&payload[3U], 16384U);
    make_page(3U, model_id, false, analysis_rate_Hz, payload, frame[3U]);

    payload[0U] = 4U;
    write_u16_le(&payload[1U], 20U);
    write_u16_le(&payload[3U], (uint16_t)(int16_t)-16384);
    make_page(4U, model_id, false, analysis_rate_Hz, payload, frame[4U]);
}

static bool feed_all(H743MeasurementRx *receiver,
                     uint8_t frame[5][H743_MEASUREMENT_FRAME_SIZE])
{
    bool published = false;

    for (unsigned int page = 0U; page < 5U; ++page)
    {
        published = H743MeasurementRx_ProcessFrame(
            receiver, frame[page], H743_MEASUREMENT_FRAME_SIZE);
    }
    return published;
}

static void test_complete_model_is_published_after_session_binding(void)
{
    H743MeasurementRx receiver;
    H743MeasurementModel model;
    uint8_t frame[5][H743_MEASUREMENT_FRAME_SIZE];

    make_valid_model_pages(1U, 2048780U, 0x80U, frame);
    H743MeasurementRx_Init(&receiver);
    H743MeasurementRx_SetRequestGeneration(&receiver, 1U);
    CHECK_TRUE(!H743MeasurementRx_TakeSessionStart(&receiver));
    CHECK_TRUE(feed_all(&receiver, frame));
    CHECK_TRUE(H743MeasurementRx_GetPublished(&receiver, &model));
    CHECK_U32(100000U, model.fundamental_Hz);
    CHECK_U32(250000U, model.upp_uV);
    CHECK_U32(2048780U, model.analysis_sample_rate_Hz);
    CHECK_U32(1U, model.model_id);
    CHECK_TRUE(H743MeasurementRx_TakeSessionStart(&receiver));
    CHECK_TRUE(!H743MeasurementRx_TakeSessionStart(&receiver));
}

static void test_unmarked_pages_before_binding_are_ignored(void)
{
    H743MeasurementRx receiver;
    H743MeasurementModel model;
    uint8_t old_model[5][H743_MEASUREMENT_FRAME_SIZE];
    uint8_t new_model[5][H743_MEASUREMENT_FRAME_SIZE];

    make_valid_model_pages(0U, 2048780U, 0x00U, old_model);
    H743MeasurementRx_Init(&receiver);
    H743MeasurementRx_SetRequestGeneration(&receiver, 1U);
    CHECK_TRUE(!feed_all(&receiver, old_model));
    CHECK_TRUE(!H743MeasurementRx_GetPublished(&receiver, &model));

    make_valid_model_pages(1U, 2048780U, 0x80U, new_model);
    CHECK_TRUE(feed_all(&receiver, new_model));
    CHECK_TRUE(H743MeasurementRx_GetPublished(&receiver, &model));
    CHECK_U32(1U, model.model_id);
}

static void test_payload_and_control_bitflips_are_rejected(void)
{
    H743MeasurementRx receiver;
    H743MeasurementModel model;
    uint8_t frame[5][H743_MEASUREMENT_FRAME_SIZE];

    make_valid_model_pages(2U, 2048780U, 0x80U, frame);
    H743MeasurementRx_Init(&receiver);
    H743MeasurementRx_SetRequestGeneration(&receiver, 1U);

    /* 载荷位翻转：不重算 CRC，必须整帧拒绝。 */
    frame[2][16U] ^= 0x01U;
    CHECK_TRUE(!H743MeasurementRx_ProcessFrame(
        &receiver, frame[0], H743_MEASUREMENT_FRAME_SIZE));
    CHECK_TRUE(!H743MeasurementRx_ProcessFrame(
        &receiver, frame[1], H743_MEASUREMENT_FRAME_SIZE));
    CHECK_TRUE(!H743MeasurementRx_ProcessFrame(
        &receiver, frame[2], H743_MEASUREMENT_FRAME_SIZE));
    CHECK_TRUE(!H743MeasurementRx_ProcessFrame(
        &receiver, frame[3], H743_MEASUREMENT_FRAME_SIZE));
    CHECK_TRUE(!H743MeasurementRx_ProcessFrame(
        &receiver, frame[4], H743_MEASUREMENT_FRAME_SIZE));
    CHECK_TRUE(!H743MeasurementRx_GetPublished(&receiver, &model));

    /* 完整新结果可以恢复，且发布后载荷正确。 */
    make_valid_model_pages(3U, 2048780U, 0x80U, frame);
    CHECK_TRUE(feed_all(&receiver, frame));
    CHECK_TRUE(H743MeasurementRx_GetPublished(&receiver, &model));
    CHECK_U32(100000U, model.fundamental_Hz);

    /* 控制字段被改写（页号跳到未预期页）即使 CRC 正确也不能拼接。 */
    frame[3][3U] = (uint8_t)((frame[3][3U] & 0xE3U) | (2U << 2U));
    seal(frame[3]);
    CHECK_TRUE(!H743MeasurementRx_ProcessFrame(
        &receiver, frame[3], H743_MEASUREMENT_FRAME_SIZE));
    CHECK_TRUE(H743MeasurementRx_GetPublished(&receiver, &model));
    CHECK_U32(100000U, model.fundamental_Hz);
}

static void test_analysis_rate_mismatch_breaks_page_group(void)
{
    H743MeasurementRx receiver;
    H743MeasurementModel model;
    uint8_t frame[5][H743_MEASUREMENT_FRAME_SIZE];

    make_valid_model_pages(1U, 2048780U, 0x80U, frame);
    H743MeasurementRx_Init(&receiver);
    H743MeasurementRx_SetRequestGeneration(&receiver, 1U);
    CHECK_TRUE(!H743MeasurementRx_ProcessFrame(
        &receiver, frame[0], H743_MEASUREMENT_FRAME_SIZE));
    write_u32_le(&frame[1][12U], 10500000U);
    seal(frame[1]);
    CHECK_TRUE(!H743MeasurementRx_ProcessFrame(
        &receiver, frame[1], H743_MEASUREMENT_FRAME_SIZE));
    CHECK_TRUE(!H743MeasurementRx_ProcessFrame(
        &receiver, frame[2], H743_MEASUREMENT_FRAME_SIZE));
    CHECK_TRUE(!H743MeasurementRx_GetPublished(&receiver, &model));
}

static void test_complete_error_model_is_not_displayable(void)
{
    H743MeasurementRx receiver;
    H743MeasurementModel model;
    uint8_t frame[5][H743_MEASUREMENT_FRAME_SIZE];
    uint8_t payload[6] = {0U};

    make_page(0U, 2U, true, 2048780U, payload, frame[0U]);
    payload[3U] = 2U;
    make_page(1U, 2U, false, 2048780U, payload, frame[1U]);
    (void)memset(payload, 0, sizeof(payload));
    for (unsigned int page = 2U; page < 5U; ++page)
    {
        make_page((uint8_t)page, 2U, false, 2048780U, payload, frame[page]);
    }

    H743MeasurementRx_Init(&receiver);
    H743MeasurementRx_SetRequestGeneration(&receiver, 1U);
    CHECK_TRUE(feed_all(&receiver, frame));
    CHECK_TRUE(H743MeasurementRx_GetPublished(&receiver, &model));
    CHECK_U32(2U, model.status);
    CHECK_TRUE(!H743MeasurementModel_IsDisplayable(&model));
}

static void test_wrong_request_generation_is_rejected(void)
{
    H743MeasurementRx receiver;
    H743MeasurementModel model;
    uint8_t frame[5][H743_MEASUREMENT_FRAME_SIZE];

    make_valid_model_pages(1U, 2048780U, 0x80U, frame);
    H743MeasurementRx_Init(&receiver);
    H743MeasurementRx_SetRequestGeneration(&receiver, 2U);
    CHECK_TRUE(!feed_all(&receiver, frame));
    CHECK_TRUE(!H743MeasurementRx_GetPublished(&receiver, &model));
    CHECK_TRUE(!H743MeasurementRx_TakeSessionStart(&receiver));
}

int main(void)
{
    test_complete_model_is_published_after_session_binding();
    test_unmarked_pages_before_binding_are_ignored();
    test_payload_and_control_bitflips_are_rejected();
    test_analysis_rate_mismatch_breaks_page_group();
    test_wrong_request_generation_is_rejected();
    test_complete_error_model_is_not_displayable();
    if (g_failures != 0U)
    {
        (void)fprintf(stderr, "%u receiver test(s) failed\n", g_failures);
        return 1;
    }
    (void)puts("h743_measurement_rx_tests: PASS");
    return 0;
}
