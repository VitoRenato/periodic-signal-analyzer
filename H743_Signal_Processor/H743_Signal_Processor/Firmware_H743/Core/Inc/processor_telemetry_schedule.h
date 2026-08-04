#ifndef PROCESSOR_TELEMETRY_SCHEDULE_H
#define PROCESSOR_TELEMETRY_SCHEDULE_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    PROCESSOR_TELEMETRY_PROCESS_SAMPLES = 0,
    PROCESSOR_TELEMETRY_ENCODE_NEXT_PAGE,
    PROCESSOR_TELEMETRY_REARM_ONLY
} ProcessorTelemetryAction;

typedef struct
{
    uint8_t frames_remaining;
} ProcessorTelemetrySchedule;

void ProcessorTelemetrySchedule_Init(ProcessorTelemetrySchedule *schedule);
void ProcessorTelemetrySchedule_Start(
    ProcessorTelemetrySchedule *schedule,
    uint8_t frame_count);
ProcessorTelemetryAction ProcessorTelemetrySchedule_OnExchangeComplete(
    ProcessorTelemetrySchedule *schedule);
uint8_t ProcessorTelemetrySchedule_Remaining(
    const ProcessorTelemetrySchedule *schedule);

#endif
