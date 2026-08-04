#include "processor_app.h"

#include "h743_measurement_pages.h"
#include "h743_set_fs_protocol.h"
#include "main.h"
#include "processor_telemetry_schedule.h"
#include "spi_links.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define PROCESSOR_APP_DEFAULT_REQUEST_HZ    2048780U
#define PROCESSOR_APP_RETRY_DELAY_MS        1U

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
 *   [0..15]    = 16 B 命令槽 (F4 端读 MISO, 协议用前 10 B, 后 6 B 留 0);
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
static H743MeasurementPager g_measurement_pager;
static ProcessorTelemetrySchedule g_telemetry_schedule;

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

static void ProcessorApp_EnterRecovery(ProcessorAppError error)
{
    ProcessorApp_RecordError(error);
    SPI_Links_AbortAll();
    g_deadline_ms = HAL_GetTick() + PROCESSOR_APP_RETRY_DELAY_MS;
    g_state = PROCESSOR_STATE_ARM_FRAME;
}

static uint32_t ProcessorApp_SelectNextRequest(const SignalResult *result)
{
    if ((result->recommended_sample_rate_Hz >= H743_SET_FS_MIN_HZ)
        && (result->recommended_sample_rate_Hz
            <= H743_SET_FS_MAX_REQUEST_HZ))
    {
        return result->recommended_sample_rate_Hz;
    }

    /*
     * 没有新建议时重发"上一次请求值", 而不是实际采样率.
     */
    return g_diagnostics.last_requested_sample_rate_Hz;
}

void ProcessorApp_Init(SPI_HandleTypeDef *spi)
{
    (void)memset(&g_diagnostics, 0, sizeof(g_diagnostics));
    (void)memset(g_dma_frame, 0, sizeof(g_dma_frame));
    (void)memset(g_dma_set_fs_frame, 0, sizeof(g_dma_set_fs_frame));
    H743MeasurementPager_Init(&g_measurement_pager);
    ProcessorTelemetrySchedule_Init(&g_telemetry_schedule);

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
                const ProcessorTelemetryAction action =
                    ProcessorTelemetrySchedule_OnExchangeComplete(
                        &g_telemetry_schedule);

                ++g_diagnostics.frames_received;
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
            else if (event == SPI_LINK_EVENT_ERROR)
            {
                ProcessorApp_EnterRecovery(
                    PROCESSOR_APP_ERROR_SAMPLE_RX_TRANSFER);
            }
            break;

        case PROCESSOR_STATE_PROCESS_FRAME:
        {
            const uint16_t *samples =
                (const uint16_t *)&g_dma_frame[SPI_LINKS_CMD_SLOT_BYTES];
            const uint32_t processing_started_ms = HAL_GetTick();
            SignalResult result;

            (void)memset(&result, 0, sizeof(result));
            g_diagnostics.last_signal_status = SignalAnalyzer_Process(
                samples,
                g_diagnostics.current_sample_rate_Hz,
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

            /* 用算法推荐的 Fs 生成下一帧的 SET_FS 命令，同时捎带 H7 测得的基频和 Vpp。 */
            uint32_t pending_request_Hz =
                ProcessorApp_SelectNextRequest(&result);

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
            g_diagnostics.current_sample_rate_Hz =
                H743SetFsProtocol_ActualRate(pending_request_Hz);
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
