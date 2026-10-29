#ifndef SPI_LINKS_H
#define SPI_LINKS_H

#include "stm32h7xx_hal.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPI_LINKS_FRAME_BYTES       8214U
#define SPI_LINKS_CMD_SLOT_BYTES    22U
#define SPI_LINKS_SAMPLE_BYTES      8192U

typedef enum
{
    SPI_LINK_EVENT_NONE = 0,
    SPI_LINK_EVENT_COMPLETE,
    SPI_LINK_EVENT_ERROR
} SpiLinkEvent;

void SPI_Links_Init(SPI_HandleTypeDef *spi);

/**
 * @brief 启动 SPI1 slave 全双工 DMA，与 F4 交换 8212 B 帧。
 * @param rx_frame  接收缓冲，长度必须 ≥ SPI_LINKS_FRAME_BYTES。
 *                  传输完成后：
 *                  - [0..19] 为 F407 主机发来的 20 B 帧头，可忽略；
 *                  - [20..8211] 为 4096 点样本（小端 16 bit，低 12 位有效）。
 * @param tx_cmd    要发送给 F4 的 20 B 命令帧（会被复制到内部 TX 缓冲）。
 * @param length    期望长度，必须为 SPI_LINKS_FRAME_BYTES。
 * @return HAL_OK 成功。
 */
HAL_StatusTypeDef SPI_Links_StartFrameExchange(
    uint8_t *rx_frame,
    const uint8_t *tx_cmd,
    uint16_t length);

SpiLinkEvent SPI_Links_TakeFrameEvent(void);

bool SPI_Links_IsMainReady(void);
uint32_t SPI_Links_GetLastHalError(void);
void SPI_Links_AbortAll(void);

/**
 * @brief 检查进行中的帧接收是否已收到部分字节但停滞超时（例如F407传输中途复位）。
 *        若尚未收到任何字节，F407可能处于冻结状态，不算超时。
 */
bool SPI_Links_HasPartialTransferTimedOut(uint32_t now_ms, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
