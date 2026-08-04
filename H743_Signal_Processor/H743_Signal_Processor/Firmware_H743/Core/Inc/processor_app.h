#ifndef PROCESSOR_APP_H
#define PROCESSOR_APP_H

#include "signal_analyzer.h"
#include "stm32h7xx_hal.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    PROCESSOR_APP_ERROR_NONE = 0,
    PROCESSOR_APP_ERROR_SAMPLE_RX_START,
    PROCESSOR_APP_ERROR_SAMPLE_RX_TRANSFER,
    PROCESSOR_APP_ERROR_CMD_TX_START,
    PROCESSOR_APP_ERROR_CMD_TX_TRANSFER,
    PROCESSOR_APP_ERROR_SET_FS_ENCODE
} ProcessorAppError;

typedef struct
{
    uint32_t frames_received;
    uint32_t frames_processed;
    uint32_t set_fs_sent;
    uint32_t error_count;
    uint32_t current_sample_rate_Hz;
    uint32_t last_requested_sample_rate_Hz;
    uint32_t sequence;
    uint32_t last_hal_error;
    uint32_t recovered_frame_count;
    uint32_t unrecoverable_clip_count;
    uint32_t frames_skipped_while_paging;
    uint32_t last_processing_ms;
    uint8_t telemetry_page;
    uint8_t telemetry_model_id;
    uint8_t telemetry_frames_remaining;
    SignalStatus last_signal_status;
    ProcessorAppError last_app_error;
} ProcessorAppDiagnostics;

void ProcessorApp_Init(SPI_HandleTypeDef *spi);

void ProcessorApp_RunOnce(void);

const ProcessorAppDiagnostics *ProcessorApp_GetDiagnostics(void);

#ifdef __cplusplus
}
#endif

#endif
