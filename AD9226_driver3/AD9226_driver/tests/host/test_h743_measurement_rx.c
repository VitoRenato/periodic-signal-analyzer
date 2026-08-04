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

static uint16_t crc16_ccitt_false(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xffffU;
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

static void make_page(
    uint8_t page,
    uint8_t model_id,
    const uint8_t payload[6],
    uint8_t frame[16])
{
    uint16_t crc;

    (void)memset(frame, 0, 16U);
    frame[0U] = 0x5aU;
    frame[1U] = 0xa5U;
    frame[2U] = 0x01U;
    frame[3U] = (uint8_t)(0x03U | (uint8_t)(page << 2U)
        | (uint8_t)(model_id << 5U));
    write_u32_le(&frame[4U], 2048780U);
    crc = crc16_ccitt_false(frame, 8U);
    write_u16_le(&frame[8U], crc);
    (void)memcpy(&frame[10U], payload, 6U);
}

static void make_valid_model_pages(uint8_t model_id, uint8_t frame[5][16])
{
    uint8_t payload[6] = {0U};

    write_u32_le(&payload[0U], 100000U);
    write_u16_le(&payload[4U], 250U);
    make_page(0U, model_id, payload, frame[0U]);

    (void)memset(payload, 0, sizeof(payload));
    write_u16_le(&payload[0U], 88U);
    payload[2U] = 3U;
    payload[3U] = 0U;
    payload[4U] = 3U;
    make_page(1U, model_id, payload, frame[1U]);

    (void)memset(payload, 0, sizeof(payload));
    payload[0U] = 1U;
    write_u16_le(&payload[1U], 100U);
    write_u16_le(&payload[3U], 0U);
    payload[5U] = 1U;
    make_page(2U, model_id, payload, frame[2U]);

    payload[0U] = 3U;
    write_u16_le(&payload[1U], 30U);
    write_u16_le(&payload[3U], 16384U);
    make_page(3U, model_id, payload, frame[3U]);

    payload[0U] = 4U;
    write_u16_le(&payload[1U], 20U);
    write_u16_le(&payload[3U], (uint16_t)(int16_t)-16384);
    make_page(4U, model_id, payload, frame[4U]);
}

static void make_clipped_model_pages(uint8_t model_id, uint8_t frame[5][16])
{
    uint8_t payload[6] = {0U};
    unsigned int page;

    make_page(0U, model_id, payload, frame[0U]);
    payload[3U] = 2U;
    make_page(1U, model_id, payload, frame[1U]);
    (void)memset(payload, 0, sizeof(payload));
    for (page = 2U; page < 5U; ++page)
    {
        make_page((uint8_t)page, model_id, payload, frame[page]);
    }
}

static void test_complete_model_is_published_atomically(void)
{
    H743MeasurementRx receiver;
    H743MeasurementModel model;
    uint8_t frame[5][16];
    unsigned int page;

    make_valid_model_pages(5U, frame);
    H743MeasurementRx_Init(&receiver);
    for (page = 0U; page < 4U; ++page)
    {
        CHECK_TRUE(!H743MeasurementRx_ProcessFrame(
            &receiver, frame[page], 16U));
        CHECK_TRUE(!H743MeasurementRx_GetPublished(&receiver, &model));
    }
    CHECK_TRUE(H743MeasurementRx_ProcessFrame(&receiver, frame[4U], 16U));
    CHECK_TRUE(H743MeasurementRx_GetPublished(&receiver, &model));
    CHECK_U32(100000U, model.fundamental_Hz);
    CHECK_U32(250000U, model.upp_uV);
    CHECK_U32(88000U, model.urms_uV);
    CHECK_U32(3U, model.component_count);
    CHECK_U32(5U, model.model_id);
    CHECK_U32(1U, model.published_sequence);
    CHECK_U32(1U, model.component[0U].order);
    CHECK_U32(100000U, model.component[0U].amplitude_peak_uV);
    CHECK_U32(3U, model.component[1U].order);
    CHECK_U32(16384U, (uint16_t)model.component[1U].phase_q15);
    CHECK_U32(4U, model.component[2U].order);
    CHECK_U32((uint16_t)(int16_t)-16384,
        (uint16_t)model.component[2U].phase_q15);
}

static void test_bad_or_cross_model_pages_do_not_pollute_published_model(void)
{
    H743MeasurementRx receiver;
    H743MeasurementModel before;
    H743MeasurementModel after;
    uint8_t first[5][16];
    uint8_t second[5][16];
    unsigned int page;

    make_valid_model_pages(1U, first);
    make_valid_model_pages(2U, second);
    H743MeasurementRx_Init(&receiver);
    for (page = 0U; page < 5U; ++page)
    {
        (void)H743MeasurementRx_ProcessFrame(&receiver, first[page], 16U);
    }
    CHECK_TRUE(H743MeasurementRx_GetPublished(&receiver, &before));

    CHECK_TRUE(!H743MeasurementRx_ProcessFrame(&receiver, second[0U], 16U));
    CHECK_TRUE(!H743MeasurementRx_ProcessFrame(&receiver, second[2U], 16U));
    second[1U][8U] ^= 0x01U;
    CHECK_TRUE(!H743MeasurementRx_ProcessFrame(&receiver, second[1U], 16U));
    CHECK_TRUE(H743MeasurementRx_GetPublished(&receiver, &after));
    CHECK_TRUE(memcmp(&before, &after, sizeof(before)) == 0);
}

static void test_invalid_component_semantics_are_rejected(void)
{
    H743MeasurementRx receiver;
    H743MeasurementModel model;
    uint8_t frame[5][16];
    unsigned int page;

    make_valid_model_pages(3U, frame);
    frame[3U][10U] = 0U;
    write_u16_le(&frame[3U][8U], crc16_ccitt_false(frame[3U], 8U));
    H743MeasurementRx_Init(&receiver);
    for (page = 0U; page < 5U; ++page)
    {
        (void)H743MeasurementRx_ProcessFrame(&receiver, frame[page], 16U);
    }
    CHECK_TRUE(!H743MeasurementRx_GetPublished(&receiver, &model));

    make_valid_model_pages(4U, frame);
    frame[1U][12U] = 4U;
    write_u16_le(&frame[1U][8U], crc16_ccitt_false(frame[1U], 8U));
    H743MeasurementRx_Init(&receiver);
    for (page = 0U; page < 5U; ++page)
    {
        (void)H743MeasurementRx_ProcessFrame(&receiver, frame[page], 16U);
    }
    CHECK_TRUE(!H743MeasurementRx_GetPublished(&receiver, &model));
}

static void test_complete_error_model_is_not_displayable(void)
{
    H743MeasurementRx receiver;
    H743MeasurementModel model;
    uint8_t frame[5][16];
    unsigned int page;

    make_clipped_model_pages(6U, frame);
    H743MeasurementRx_Init(&receiver);
    for (page = 0U; page < 5U; ++page)
    {
        (void)H743MeasurementRx_ProcessFrame(&receiver, frame[page], 16U);
    }
    CHECK_TRUE(H743MeasurementRx_GetPublished(&receiver, &model));
    CHECK_U32(2U, model.status);
    CHECK_U32(0U, model.fundamental_Hz);
    CHECK_U32(0U, model.component_count);
    CHECK_TRUE(!H743MeasurementModel_IsDisplayable(&model));

    make_valid_model_pages(7U, frame);
    H743MeasurementRx_Init(&receiver);
    for (page = 0U; page < 5U; ++page)
    {
        (void)H743MeasurementRx_ProcessFrame(&receiver, frame[page], 16U);
    }
    CHECK_TRUE(H743MeasurementRx_GetPublished(&receiver, &model));
    CHECK_TRUE(H743MeasurementModel_IsDisplayable(&model));
}

int main(void)
{
    test_complete_model_is_published_atomically();
    test_bad_or_cross_model_pages_do_not_pollute_published_model();
    test_invalid_component_semantics_are_rejected();
    test_complete_error_model_is_not_displayable();
    if (g_failures != 0U)
    {
        (void)fprintf(stderr, "%u receiver test(s) failed\n", g_failures);
        return 1;
    }
    (void)puts("h743_measurement_rx_tests: PASS");
    return 0;
}
