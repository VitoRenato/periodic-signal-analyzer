#include "h743_measurement_pages.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_PI 3.14159265358979323846

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
        const uint32_t expected_value = (uint32_t)(expected);                   \
        const uint32_t actual_value = (uint32_t)(actual);                       \
        if (expected_value != actual_value)                                     \
        {                                                                       \
            (void)fprintf(stderr,                                               \
                "FAIL %s:%d: expected %lu, got %lu\n",                       \
                __FILE__, __LINE__, (unsigned long)expected_value,              \
                (unsigned long)actual_value);                                   \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

static uint16_t read_u16_le(const uint8_t bytes[2])
{
    return (uint16_t)((uint16_t)bytes[0]
        | ((uint16_t)bytes[1] << 8U));
}

static uint32_t read_u32_le(const uint8_t bytes[4])
{
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8U)
        | ((uint32_t)bytes[2] << 16U)
        | ((uint32_t)bytes[3] << 24U);
}

static void test_five_pages_share_one_model_snapshot(void)
{
    SignalResult result = {0};
    H743MeasurementPager pager;
    uint8_t frame[H743_SET_FS_FRAME_SIZE];
    uint8_t model_id = 0xffU;
    unsigned int page;

    result.status = SIGNAL_STATUS_VALID;
    result.fundamental_Hz = 100000.0f;
    result.upp_V = 0.250f;
    result.urms_V = 0.088f;
    result.component_count = 3U;
    result.quality_flags = SIGNAL_QUALITY_LOWER_CLIP_DETECTED
        | SIGNAL_QUALITY_CLIPPED_MODEL_RECOVERED;
    result.component[0] = (SignalComponent){100000.0f, 0.100f, 0.0f};
    result.component[1] = (SignalComponent){300000.0f, 0.030f,
        (float)(0.5 * TEST_PI)};
    result.component[2] = (SignalComponent){400000.0f, 0.020f,
        (float)(-0.5 * TEST_PI)};

    H743MeasurementPager_Init(&pager);
    CHECK_TRUE(H743MeasurementPager_Load(&pager, &result, 2048780U));

    for (page = 0U; page < H743_MEASUREMENT_PAGE_COUNT; ++page)
    {
        uint8_t flags;

        CHECK_TRUE(H743MeasurementPager_EncodeNext(&pager, frame));
        flags = frame[3U];
        CHECK_TRUE((flags & H743_MEASUREMENT_FLAG_VALID) != 0U);
        CHECK_TRUE((flags & H743_MEASUREMENT_FLAG_RECOVERED) != 0U);
        CHECK_U32(page, (flags >> H743_MEASUREMENT_PAGE_SHIFT) & 0x07U);
        if (page == 0U)
        {
            model_id = (uint8_t)(flags >> H743_MEASUREMENT_MODEL_SHIFT);
            CHECK_U32(100000U, read_u32_le(&frame[10U]));
            CHECK_U32(250U, read_u16_le(&frame[14U]));
        }
        else
        {
            CHECK_U32(model_id,
                (uint8_t)(flags >> H743_MEASUREMENT_MODEL_SHIFT));
        }

        if (page == 1U)
        {
            CHECK_U32(88U, read_u16_le(&frame[10U]));
            CHECK_U32(3U, frame[12U]);
            CHECK_U32(SIGNAL_STATUS_VALID, frame[13U]);
            CHECK_U32(result.quality_flags, frame[14U]);
            CHECK_U32(0U, frame[15U]);
        }
        else if (page >= 2U)
        {
            static const uint8_t expected_order[3] = {1U, 3U, 4U};
            static const uint16_t expected_amplitude_mV[3] = {
                100U, 30U, 20U
            };
            static const int16_t expected_phase_q15[3] = {
                0, 16384, -16384
            };
            const size_t component = page - 2U;

            CHECK_U32(expected_order[component], frame[10U]);
            CHECK_U32(expected_amplitude_mV[component],
                read_u16_le(&frame[11U]));
            CHECK_U32((uint16_t)expected_phase_q15[component],
                read_u16_le(&frame[13U]));
            CHECK_U32(1U, frame[15U]);
        }
    }
}

static SignalResult make_snapshot_result(uint32_t fundamental_Hz)
{
    SignalResult result = {0};

    result.status = SIGNAL_STATUS_VALID;
    result.fundamental_Hz = (float)fundamental_Hz;
    result.upp_V = (float)fundamental_Hz / 1000000.0f;
    result.urms_V = (float)fundamental_Hz / 2000000.0f;
    result.component_count = 1U;
    result.component[0] = (SignalComponent){
        (float)fundamental_Hz,
        0.050f,
        0.0f
    };
    return result;
}

static void test_new_results_do_not_mix_pages_inside_a_cycle(void)
{
    H743MeasurementPager pager;
    uint8_t frame[H743_SET_FS_FRAME_SIZE];
    uint8_t first_model_id = 0xffU;
    uint8_t second_model_id = 0xffU;
    unsigned int page;

    H743MeasurementPager_Init(&pager);
    for (page = 0U; page < 5U; ++page)
    {
        const SignalResult result = make_snapshot_result(100000U + page);

        CHECK_TRUE(H743MeasurementPager_Load(&pager, &result, 2048780U));
        CHECK_TRUE(H743MeasurementPager_EncodeNext(&pager, frame));
        CHECK_U32(page, (frame[3U] >> H743_MEASUREMENT_PAGE_SHIFT) & 0x07U);
        if (page == 0U)
        {
            first_model_id = (uint8_t)(frame[3U]
                >> H743_MEASUREMENT_MODEL_SHIFT);
            CHECK_U32(100000U, read_u32_le(&frame[10U]));
        }
        else
        {
            CHECK_U32(first_model_id,
                (uint8_t)(frame[3U] >> H743_MEASUREMENT_MODEL_SHIFT));
        }
        if (page == 1U)
        {
            CHECK_U32(50U, read_u16_le(&frame[10U]));
        }
    }

    for (page = 0U; page < 5U; ++page)
    {
        const SignalResult result = make_snapshot_result(200000U + page);

        CHECK_TRUE(H743MeasurementPager_Load(&pager, &result, 2048780U));
        CHECK_TRUE(H743MeasurementPager_EncodeNext(&pager, frame));
        if (page == 0U)
        {
            second_model_id = (uint8_t)(frame[3U]
                >> H743_MEASUREMENT_MODEL_SHIFT);
            CHECK_TRUE(second_model_id != first_model_id);
            CHECK_U32(200000U, read_u32_le(&frame[10U]));
        }
        else
        {
            CHECK_U32(second_model_id,
                (uint8_t)(frame[3U] >> H743_MEASUREMENT_MODEL_SHIFT));
        }
        if (page == 1U)
        {
            CHECK_U32(100U, read_u16_le(&frame[10U]));
        }
    }
}

static void test_two_components_leave_third_slot_invalid(void)
{
    SignalResult result = {0};
    H743MeasurementPager pager;
    uint8_t frame[H743_SET_FS_FRAME_SIZE];
    unsigned int page;

    result.status = SIGNAL_STATUS_VALID;
    result.fundamental_Hz = 125000.0f;
    result.upp_V = 0.180f;
    result.urms_V = 0.070f;
    result.component_count = 2U;
    result.component[0] = (SignalComponent){125000.0f, 0.080f, 0.25f};
    result.component[1] = (SignalComponent){250000.0f, 0.025f, -0.75f};

    H743MeasurementPager_Init(&pager);
    CHECK_TRUE(H743MeasurementPager_Load(&pager, &result, 2048780U));
    for (page = 0U; page < H743_MEASUREMENT_PAGE_COUNT; ++page)
    {
        CHECK_TRUE(H743MeasurementPager_EncodeNext(&pager, frame));
        if (page == 2U)
        {
            CHECK_U32(1U, frame[10U]);
            CHECK_U32(1U, frame[15U]);
        }
        else if (page == 3U)
        {
            CHECK_U32(2U, frame[10U]);
            CHECK_U32(1U, frame[15U]);
        }
        else if (page == 4U)
        {
            CHECK_U32(0U, frame[10U]);
            CHECK_U32(0U, read_u16_le(&frame[11U]));
            CHECK_U32(0U, read_u16_le(&frame[13U]));
            CHECK_U32(0U, frame[15U]);
        }
    }
}

int main(void)
{
    test_five_pages_share_one_model_snapshot();
    test_new_results_do_not_mix_pages_inside_a_cycle();
    test_two_components_leave_third_slot_invalid();
    if (g_failures != 0U)
    {
        (void)fprintf(stderr, "%u measurement page test(s) failed\n",
            g_failures);
        return 1;
    }
    (void)puts("measurement_pages_tests: PASS");
    return 0;
}
