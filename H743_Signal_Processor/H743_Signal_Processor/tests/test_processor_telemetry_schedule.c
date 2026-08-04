#include "processor_telemetry_schedule.h"

#include <stdint.h>
#include <stdio.h>

static unsigned int g_failures;

#define CHECK_U32(expected, actual)                                             \
    do                                                                          \
    {                                                                           \
        const uint32_t expected_value = (uint32_t)(expected);                   \
        const uint32_t actual_value = (uint32_t)(actual);                       \
        if (expected_value != actual_value)                                     \
        {                                                                       \
            (void)fprintf(stderr,                                               \
                          "FAIL %s:%d expected %lu got %lu\n",                \
                          __FILE__, __LINE__,                                    \
                          (unsigned long)expected_value,                         \
                          (unsigned long)actual_value);                          \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

static void Test_OneProcessThenFiveTelemetryFrames(void)
{
    ProcessorTelemetrySchedule schedule;
    static const ProcessorTelemetryAction expected[5] = {
        PROCESSOR_TELEMETRY_ENCODE_NEXT_PAGE,
        PROCESSOR_TELEMETRY_ENCODE_NEXT_PAGE,
        PROCESSOR_TELEMETRY_ENCODE_NEXT_PAGE,
        PROCESSOR_TELEMETRY_ENCODE_NEXT_PAGE,
        PROCESSOR_TELEMETRY_REARM_ONLY
    };
    uint32_t process_count = 0U;
    size_t index;

    ProcessorTelemetrySchedule_Init(&schedule);
    CHECK_U32(PROCESSOR_TELEMETRY_PROCESS_SAMPLES,
        ProcessorTelemetrySchedule_OnExchangeComplete(&schedule));
    ++process_count;

    ProcessorTelemetrySchedule_Start(&schedule, 5U);
    CHECK_U32(5U, ProcessorTelemetrySchedule_Remaining(&schedule));
    for (index = 0U; index < 5U; ++index)
    {
        CHECK_U32(expected[index],
            ProcessorTelemetrySchedule_OnExchangeComplete(&schedule));
        CHECK_U32(4U - (uint32_t)index,
            ProcessorTelemetrySchedule_Remaining(&schedule));
    }
    CHECK_U32(1U, process_count);
    CHECK_U32(PROCESSOR_TELEMETRY_PROCESS_SAMPLES,
        ProcessorTelemetrySchedule_OnExchangeComplete(&schedule));
    ++process_count;
    CHECK_U32(2U, process_count);
}

static void Test_NullScheduleIsSafe(void)
{
    ProcessorTelemetrySchedule_Init(NULL);
    ProcessorTelemetrySchedule_Start(NULL, 5U);
    CHECK_U32(PROCESSOR_TELEMETRY_REARM_ONLY,
        ProcessorTelemetrySchedule_OnExchangeComplete(NULL));
    CHECK_U32(0U, ProcessorTelemetrySchedule_Remaining(NULL));
}

int main(void)
{
    Test_OneProcessThenFiveTelemetryFrames();
    Test_NullScheduleIsSafe();
    if (g_failures != 0U)
    {
        (void)fprintf(stderr, "%u failure(s)\n", g_failures);
        return 1;
    }
    (void)puts("processor telemetry schedule tests passed");
    return 0;
}
