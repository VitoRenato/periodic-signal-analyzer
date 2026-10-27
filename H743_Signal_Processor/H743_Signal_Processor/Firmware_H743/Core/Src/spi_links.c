#include "spi_links.h"

#include "main.h"
#include "h743_set_fs_protocol.h"

#if defined(__GNUC__) || defined(__clang__) || defined(__CC_ARM)
#define SPI_LINKS_DMA_BUFFER __attribute__((section(".dma_buffer"), aligned(32)))
#else
#error "DMA buffer section and 32-byte alignment must be defined for this compiler"
#endif

typedef enum
{
    SPI_OPERATION_IDLE = 0,
    SPI_OPERATION_FRAME_EXCHANGE
} SpiOperation;

/*
 * H7 从机通过 SPI1 全双工与 F4 交换一帧 8208 B：
 *   MOSI (RX): [16 B 命令槽占位][8192 B 样本]
 *   MISO (TX): [16 B SET_FS 命令][8192 B 占位]
 * TX 缓冲放在 .dma_buffer 段（非缓存 D2 SRAM），与 DMA 视图一致。
 */
SPI_LINKS_DMA_BUFFER
static uint8_t s_spi1_tx_frame[SPI_LINKS_FRAME_BYTES];

static SPI_HandleTypeDef *g_spi;
static volatile SpiOperation g_active_operation;
static volatile SpiLinkEvent g_frame_event;
static volatile uint32_t g_last_hal_error;
static uint32_t g_transfer_started_ms;
static uint8_t g_transfer_observed;

/* 让 H7 在挂载 DMA 后有足够时间就绪再拉高 READY，避免 F4 提前起时钟 */
#define SPI_LINKS_READY_SETUP_DELAY_MS  1U

static void SPI_Links_SetAcqReady(GPIO_PinState state)
{
    HAL_GPIO_WritePin(ACQ_READY_GPIO_Port, ACQ_READY_Pin, state);
}

void SPI_Links_Init(SPI_HandleTypeDef *spi)
{
    g_spi = spi;
    g_active_operation = SPI_OPERATION_IDLE;
    g_frame_event = SPI_LINK_EVENT_NONE;
    g_last_hal_error = HAL_SPI_ERROR_NONE;

    (void)memset(s_spi1_tx_frame, 0, sizeof(s_spi1_tx_frame));

    /* READY 低表示 H743 尚未准备好一次帧交换。 */
    SPI_Links_SetAcqReady(GPIO_PIN_RESET);
}

HAL_StatusTypeDef SPI_Links_StartFrameExchange(
    uint8_t *rx_frame,
    const uint8_t *tx_cmd,
    uint16_t length)
{
    HAL_StatusTypeDef status;

    if ((g_spi == NULL)
        || (rx_frame == NULL)
        || (tx_cmd == NULL)
        || (length != SPI_LINKS_FRAME_BYTES)
        || (g_active_operation != SPI_OPERATION_IDLE))
    {
        return HAL_ERROR;
    }

    SPI_Links_SetAcqReady(GPIO_PIN_RESET);
    g_frame_event = SPI_LINK_EVENT_NONE;
    g_last_hal_error = HAL_SPI_ERROR_NONE;
    g_transfer_started_ms = 0U;
    g_transfer_observed = 0U;
    g_active_operation = SPI_OPERATION_FRAME_EXCHANGE;

    /* 把命令放在帧首 16 B，其余 8192 B 为占位（已初始化为 0）。 */
    (void)memcpy(s_spi1_tx_frame, tx_cmd, H743_SET_FS_FRAME_SIZE);

    status = HAL_SPI_TransmitReceive_DMA(
        g_spi,
        s_spi1_tx_frame,
        rx_frame,
        SPI_LINKS_FRAME_BYTES);
    if (status != HAL_OK)
    {
        g_active_operation = SPI_OPERATION_IDLE;
        return status;
    }

    /* 等待 DMA 事务稳定后再通知 F4 主机可以起时钟。 */
    HAL_Delay(SPI_LINKS_READY_SETUP_DELAY_MS);
    SPI_Links_SetAcqReady(GPIO_PIN_SET);
    return HAL_OK;
}

SpiLinkEvent SPI_Links_TakeFrameEvent(void)
{
    const SpiLinkEvent event = g_frame_event;

    if (event != SPI_LINK_EVENT_NONE)
    {
        g_frame_event = SPI_LINK_EVENT_NONE;
    }
    return event;
}

bool SPI_Links_IsMainReady(void)
{
    return HAL_GPIO_ReadPin(MAIN_READY_GPIO_Port, MAIN_READY_Pin)
        == GPIO_PIN_SET;
}

uint32_t SPI_Links_GetLastHalError(void)
{
    return g_last_hal_error;
}

void SPI_Links_AbortAll(void)
{
    SPI_Links_SetAcqReady(GPIO_PIN_RESET);

    if (g_spi != NULL)
    {
        (void)HAL_SPI_Abort(g_spi);
    }

    g_active_operation = SPI_OPERATION_IDLE;
    g_frame_event = SPI_LINK_EVENT_NONE;
    g_transfer_observed = 0U;
}

bool SPI_Links_HasPartialTransferTimedOut(uint32_t now_ms, uint32_t timeout_ms)
{
    uint32_t remaining;

    if ((g_spi == NULL)
        || (g_spi->hdmarx == NULL)
        || (g_active_operation != SPI_OPERATION_FRAME_EXCHANGE))
    {
        return false;
    }

    remaining = __HAL_DMA_GET_COUNTER(g_spi->hdmarx);
    if ((remaining == 0U) || (remaining >= SPI_LINKS_FRAME_BYTES))
    {
        return false;
    }
    if (g_transfer_observed == 0U)
    {
        g_transfer_observed = 1U;
        g_transfer_started_ms = now_ms;
        return false;
    }
    return (now_ms - g_transfer_started_ms) >= timeout_ms;
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if ((hspi == g_spi)
        && (g_active_operation == SPI_OPERATION_FRAME_EXCHANGE))
    {
        /* DMA 完成中断中立即撤销 READY，算法处理只在主循环进行。 */
        SPI_Links_SetAcqReady(GPIO_PIN_RESET);
        g_active_operation = SPI_OPERATION_IDLE;
        g_frame_event = SPI_LINK_EVENT_COMPLETE;
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    g_last_hal_error = hspi->ErrorCode;

    /* SPI 出错立刻拉低 READY，禁止 F407 继续送时钟。 */
    SPI_Links_SetAcqReady(GPIO_PIN_RESET);

    if (hspi == g_spi)
    {
        g_frame_event = SPI_LINK_EVENT_ERROR;
    }

    g_active_operation = SPI_OPERATION_IDLE;
}
