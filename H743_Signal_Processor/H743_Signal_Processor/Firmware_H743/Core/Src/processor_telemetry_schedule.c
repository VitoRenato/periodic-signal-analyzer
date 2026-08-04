#include "processor_telemetry_schedule.h"

void ProcessorTelemetrySchedule_Init(ProcessorTelemetrySchedule *schedule)
{
    if (schedule != NULL)
    {
        schedule->frames_remaining = 0U;
    }
}

void ProcessorTelemetrySchedule_Start(
    ProcessorTelemetrySchedule *schedule,
    uint8_t frame_count)
{
    if (schedule != NULL)
    {
        schedule->frames_remaining = frame_count;
    }
}

ProcessorTelemetryAction ProcessorTelemetrySchedule_OnExchangeComplete(
    ProcessorTelemetrySchedule *schedule)
{
    if (schedule == NULL)
    {
        return PROCESSOR_TELEMETRY_REARM_ONLY;
    }
    if (schedule->frames_remaining == 0U)
    {
        return PROCESSOR_TELEMETRY_PROCESS_SAMPLES;
    }

    --schedule->frames_remaining;
    return (schedule->frames_remaining == 0U)
        ? PROCESSOR_TELEMETRY_REARM_ONLY
        : PROCESSOR_TELEMETRY_ENCODE_NEXT_PAGE;
}

uint8_t ProcessorTelemetrySchedule_Remaining(
    const ProcessorTelemetrySchedule *schedule)
{
    return (schedule != NULL) ? schedule->frames_remaining : 0U;
}
