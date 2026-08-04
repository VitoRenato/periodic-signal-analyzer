#ifndef SPI_LINKS_H
#define SPI_LINKS_H

#include "stm32h7xx_hal.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPI_LINKS_FRAME_BYTES       8208U
#define SPI_LINKS_CMD_SLOT_BYTES    16U
#define SPI_LINKS_SAMPLE_BYTES      8192U

typedef enum
{
    SPI_LINK_EVENT_NONE = 0,
    SPI_LINK_EVENT_COMPLETE,
    SPI_LINK_EVENT_ERROR
} SpiLinkEvent;

void SPI_Links_Init(SPI_HandleTypeDef *spi);

/**
 * @brief 启动 SPI1 slave 全双工 DMA，与 F4 交换 8208 B 帧。
 * @param rx_frame  接收缓冲，长度必须 ≥ SPI_LINKS_FRAME_BYTES。
 *                  传输完成后：
 *                  - [0..15] 为 F407 主机发来的命令槽占位，可忽略；
 *                  - [16..8207] 为 4096 点样本（小端 16 bit，低 12 位有效）。
 * @param tx_cmd    要发送给 F4 的 16 B 命令帧（会被复制到内部 TX 缓冲）。
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

#ifdef __cplusplus
}
#endif

#endif
