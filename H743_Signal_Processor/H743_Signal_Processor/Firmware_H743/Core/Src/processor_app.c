#include "processor_app.h"

#include "acq_frame_header.h"
#include "h743_measurement_pages.h"
#include "h743_set_fs_protocol.h"
#include "main.h"
#include "processor_telemetry_schedule.h"
#include "sample_rate_policy.h"
#include "spi_links.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define PROCESSOR_APP_DEFAULT_REQUEST_HZ    SAMPLE_RATE_POLICY_DEFAULT_REQUEST_HZ
/*
 * F407 发一帧 8208 B @ 5.25 MHz 约 12.5 ms。
 * 发生错位或超时后将 READY 拉低 20 ms，确保 F407 进行中的帧已彻底结束，再重新挂载 DMA。
 */
#define PROCESSOR_APP_RESYNC_DELAY_MS       20U
#define PROCESSOR_APP_PARTIAL_TIMEOUT_MS    50U

#if defined(__GNUC__) || defined(__clang__) || defined(__CC_ARM)
#define PROCESSOR_DMA_BUFFER __attribute__((section(".dma_buffer"), aligned(32)))
#else
#error "DMA buffer section and 32-byte alignment must be defined for this compiler"
#endif

typedef enum
{
    PROCESSOR_STATE_ARM_FRAME = 0,
    PROCESSOR_STATE_WAIT_FRAME,
    PROCESSOR_STATE_PROCESS_FRAME
} ProcessorState;

/*
 * 单 SPI1 全双工 8208 B 帧缓冲:
 *   [0..15]    = 16 B 帧头 (F407 写入采样分频、帧序号与样本 CRC32);
 *   [16..8207] = 4096 点 uint16_t 样本.
 * 整段 8208 B 放在 .dma_buffer 段(MPU 标记为非缓存 D2 SRAM),
 * CPU 与 DMA 视图天然一致, 无需 SCB_Clean/Invalidate DCache.
 */
PROCESSOR_DMA_BUFFER
static uint8_t g_dma_frame[SPI_LINKS_FRAME_BYTES];

PROCESSOR_DMA_BUFFER
static uint8_t g_dma_set_fs_frame[H743_SET_FS_FRAME_SIZE];

static ProcessorState g_state;
static ProcessorAppDiagnostics g_diagnostics;
static uint32_t g_deadline_ms;
static uint8_t g_waiting_resync;
static AcqFrameHeader g_current_header;
static H743MeasurementPager g_measurement_pager;
static ProcessorTelemetrySchedule g_telemetry_schedule;
static SampleRatePolicy g_sample_rate_policy;

static bool ProcessorApp_EncodeNextTelemetry(void)
{
    if (!H743MeasurementPager_EncodeNext(
            &g_measurement_pager,
            g_dma_set_fs_frame))
    {
        return false;
    }
    g_diagnostics.telemetry_page = (uint8_t)(
        (g_measurement_pager.page + H743_MEASUREMENT_PAGE_COUNT - 1U)
        % H743_MEASUREMENT_PAGE_COUNT);
    g_diagnostics.telemetry_model_id = g_measurement_pager.model_id;
    return true;
}

static void ProcessorApp_RecordError(ProcessorAppError error)
{
    g_diagnostics.last_app_error = error;
    g_diagnostics.last_hal_error = SPI_Links_GetLastHalError();
    ++g_diagnostics.error_count;
}

static void ProcessorApp_RewindTelemetry(void)
{
    if (g_measurement_pager.active_valid != 0U)
    {
        H743MeasurementPager_Rewind(&g_measurement_pager);
        (void)ProcessorApp_EncodeNextTelemetry();
        ProcessorTelemetrySchedule_Start(
            &g_telemetry_schedule,
            H743_MEASUREMENT_PAGE_COUNT);
    }
    else
    {
        ProcessorTelemetrySchedule_Init(&g_telemetry_schedule);
        (void)H743SetFsProtocol_Encode(
            g_diagnostics.last_requested_sample_rate_Hz,
            g_dma_set_fs_frame);
    }
    g_diagnostics.telemetry_frames_remaining =
        ProcessorTelemetrySchedule_Remaining(&g_telemetry_schedule);
}

static void ProcessorApp_EnterRecovery(ProcessorAppError error)
{
    ProcessorApp_RecordError(error);
    SPI_Links_AbortAll();
    ProcessorApp_RewindTelemetry();
    g_deadline_ms = HAL_GetTick() + PROCESSOR_APP_RESYNC_DELAY_MS;
    g_waiting_resync = 1U;
    g_state = PROCESSOR_STATE_ARM_FRAME;
}

static void ProcessorApp_OnNewSessionFrame(void)
{
    SampleRatePolicy_OnNewSession(&g_sample_rate_policy);
    H743MeasurementPager_Init(&g_measurement_pager);
    ProcessorTelemetrySchedule_Init(&g_telemetry_schedule);
    g_diagnostics.telemetry_frames_remaining = 0U;
    g_diagnostics.last_requested_sample_rate_Hz =
        PROCESSOR_APP_DEFAULT_REQUEST_HZ;
}

void ProcessorApp_Init(SPI_HandleTypeDef *spi)
{
    (void)memset(&g_diagnostics, 0, sizeof(g_diagnostics));
    (void)memset(g_dma_frame, 0, sizeof(g_dma_frame));
    (void)memset(g_dma_set_fs_frame, 0, sizeof(g_dma_set_fs_frame));
    (void)memset(&g_current_header, 0, sizeof(g_current_header));
    g_waiting_resync = 0U;
    H743MeasurementPager_Init(&g_measurement_pager);
    ProcessorTelemetrySchedule_Init(&g_telemetry_schedule);
    SampleRatePolicy_Init(&g_sample_rate_policy);

    SPI_Links_Init(spi);

    /* 预编码第一帧 SET_FS 命令，在首次帧交换时发给 F4。 */
    if (!H743SetFsProtocol_Encode(
            PROCESSOR_APP_DEFAULT_REQUEST_HZ,
            g_dma_set_fs_frame))
    {
        Error_Handler();
    }

    g_diagnostics.last_requested_sample_rate_Hz =
        PROCESSOR_APP_DEFAULT_REQUEST_HZ;
    g_diagnostics.current_sample_rate_Hz =
        H743SetFsProtocol_ActualRate(PROCESSOR_APP_DEFAULT_REQUEST_HZ);
    g_diagnostics.last_signal_status = SIGNAL_STATUS_BAD_ARGUMENT;
    g_diagnostics.last_app_error = PROCESSOR_APP_ERROR_NONE;
    g_state = PROCESSOR_STATE_ARM_FRAME;
}

void ProcessorApp_RunOnce(void)
{
    SpiLinkEvent event;

    switch (g_state)
    {
        case PROCESSOR_STATE_ARM_FRAME:
            if ((g_waiting_resync != 0U)
                && ((int32_t)(HAL_GetTick() - g_deadline_ms) < 0))
            {
                break;
            }
            g_waiting_resync = 0U;
            if (SPI_Links_StartFrameExchange(
                    g_dma_frame,
                    g_dma_set_fs_frame,
                    SPI_LINKS_FRAME_BYTES) == HAL_OK)
            {
                ++g_diagnostics.set_fs_sent;
                g_state = PROCESSOR_STATE_WAIT_FRAME;
            }
            else
            {
                ProcessorApp_EnterRecovery(
                    PROCESSOR_APP_ERROR_SAMPLE_RX_START);
            }
            break;

        case PROCESSOR_STATE_WAIT_FRAME:
            event = SPI_Links_TakeFrameEvent();
            if (event == SPI_LINK_EVENT_COMPLETE)
            {
                const AcqFrameStatus header_status = AcqFrameHeader_Validate(
                    g_dma_frame,
                    &g_current_header);

                g_diagnostics.last_header_status = (uint8_t)header_status;
                if (header_status != ACQ_FRAME_OK)
                {
                    ++g_diagnostics.header_error_count;
                    ProcessorApp_EnterRecovery(
                        PROCESSOR_APP_ERROR_FRAME_HEADER);
                    break;
                }

                ++g_diagnostics.frames_received;
                g_diagnostics.current_sample_rate_Hz =
                    g_current_header.sample_rate_Hz;
                if (AcqFrameHeader_IsNewSession(&g_current_header))
                {
                    ProcessorApp_OnNewSessionFrame();
                }

                {
                    const ProcessorTelemetryAction action =
                        ProcessorTelemetrySchedule_OnExchangeComplete(
                            &g_telemetry_schedule);

                    g_diagnostics.telemetry_frames_remaining =
                        ProcessorTelemetrySchedule_Remaining(
                            &g_telemetry_schedule);
                    if (action == PROCESSOR_TELEMETRY_PROCESS_SAMPLES)
                    {
                        g_state = PROCESSOR_STATE_PROCESS_FRAME;
                    }
                    else
                    {
                        ++g_diagnostics.frames_skipped_while_paging;
                        if ((action == PROCESSOR_TELEMETRY_ENCODE_NEXT_PAGE)
                            && !ProcessorApp_EncodeNextTelemetry())
                        {
                            ProcessorApp_EnterRecovery(
                                PROCESSOR_APP_ERROR_SET_FS_ENCODE);
                            break;
                        }
                        g_state = PROCESSOR_STATE_ARM_FRAME;
                    }
                }
            }
            else if (event == SPI_LINK_EVENT_ERROR)
            {
                ProcessorApp_EnterRecovery(
                    PROCESSOR_APP_ERROR_SAMPLE_RX_TRANSFER);
            }
            else if (SPI_Links_HasPartialTransferTimedOut(
                         HAL_GetTick(),
                         PROCESSOR_APP_PARTIAL_TIMEOUT_MS))
            {
                ProcessorApp_EnterRecovery(
                    PROCESSOR_APP_ERROR_PARTIAL_TIMEOUT);
            }
            break;

        case PROCESSOR_STATE_PROCESS_FRAME:
        {
            const uint16_t *samples =
                (const uint16_t *)&g_dma_frame[SPI_LINKS_CMD_SLOT_BYTES];
            const uint32_t sample_rate_Hz = g_current_header.sample_rate_Hz;
            const uint32_t processing_started_ms = HAL_GetTick();
            SignalResult result;
            uint32_t pending_request_Hz;

            (void)memset(&result, 0, sizeof(result));
            g_diagnostics.last_signal_status = SignalAnalyzer_Process(
                samples,
                sample_rate_Hz,
                &result);
            g_diagnostics.last_processing_ms =
                HAL_GetTick() - processing_started_ms;
            ++g_diagnostics.frames_processed;
            ++g_diagnostics.sequence;

            if ((result.quality_flags
                    & SIGNAL_QUALITY_CLIPPED_MODEL_RECOVERED) != 0U)
            {
                ++g_diagnostics.recovered_frame_count;
            }
            else if (result.status == SIGNAL_STATUS_CLIPPED)
            {
                ++g_diagnostics.unrecoverable_clip_count;
            }

            pending_request_Hz = SampleRatePolicy_Next(
                &g_sample_rate_policy,
                sample_rate_Hz,
                &result);

            if (!H743MeasurementPager_Load(
                    &g_measurement_pager,
                    &result,
                    pending_request_Hz)
                || !ProcessorApp_EncodeNextTelemetry())
            {
                ProcessorApp_EnterRecovery(
                    PROCESSOR_APP_ERROR_SET_FS_ENCODE);
                break;
            }
            ProcessorTelemetrySchedule_Start(
                &g_telemetry_schedule,
                H743_MEASUREMENT_PAGE_COUNT);
            g_diagnostics.telemetry_frames_remaining =
                ProcessorTelemetrySchedule_Remaining(&g_telemetry_schedule);
            g_diagnostics.last_requested_sample_rate_Hz =
                pending_request_Hz;
            g_diagnostics.last_app_error = PROCESSOR_APP_ERROR_NONE;
            g_state = PROCESSOR_STATE_ARM_FRAME;
            break;
        }

        default:
            ProcessorApp_EnterRecovery(
                PROCESSOR_APP_ERROR_SAMPLE_RX_START);
            break;
    }
}

const ProcessorAppDiagnostics *ProcessorApp_GetDiagnostics(void)
{
    return &g_diagnostics;
}
