/**
 * @file    spi.h
 * @brief   F407 与 H743 单 SPI1 全双工通信:
 *         - SPI1 MOSI: F407 → H743 样本 (8208 B)
 *         - SPI1 MISO: F407 ← H743 命令槽 (16 B)
 * @note    MX_SPI1_Init 在 main.c, hspi1 是 main.c 全局
 */
#ifndef SPI_H
#define SPI_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

/* 命令槽长度:每帧开头 16 字节,用于 H7 向 F407 回传命令 */
#define ACQ_SPI_CMD_SLOT_BYTES  16U

/* ===== §3.1 SPI_A_READY 硬件握手 ===== *
 * F407 端 PA4 = H7_READY 输入,H7 拉高后 F407 才发起 SPI1 全双工帧交换
 * 内部上拉:H7 没接时 PA4=1,兼容"无脑发"行为(自环/冒烟不被破坏)
 * 超时保护:500ms 内没等到 READY=1,丢弃本帧 + 错误计数+1,F407 不卡死
 */
#define ACQ_SPI_READY_PORT        GPIOA
#define ACQ_SPI_READY_PIN         GPIO_PIN_4
#define ACQ_SPI_READY_TIMEOUT_MS  1500U

/**
 * @brief  通过 SPI1 全双工阻塞交换一帧 8208 B
 * @param  buf   : 样本缓冲指针 (来自 AD9226_GetBuf(), 低 12 bit 有效)
 * @param  n     : 样本数 (e.g. 4096), 必须 ≤ AD9226_SAMPLE_COUNT
 * @param  fs_hz : 当前采样率(Hz), 兼容旧接口,未使用
 * @return HAL_OK 成功, HAL_TIMEOUT 握手超时, HAL_ERROR 其他错误
 * @note   一帧总长度 = 16 B 命令槽占位 + 8192 B 样本 = 8208 B
 *         MOSI 发送样本,MISO 同时接收命令槽,命令槽保存在内部缓冲。
 */
HAL_StatusTypeDef AcqSpi_SendSamples(const uint16_t *buf, uint32_t n, uint32_t fs_hz);

/**
 * @brief  从最近一次 SPI1 全双工帧的命令槽中取出 16 B 命令
 * @param  cmd_out : 命令槽输出缓冲,长度至少 cmd_len
 * @param  cmd_len : 要取回的命令字节数,≤ ACQ_SPI_CMD_SLOT_BYTES(16)
 * @return HAL_OK 成功, HAL_ERROR 参数错误
 * @note   必须在 AcqSpi_SendSamples 成功后调用
 */
HAL_StatusTypeDef AcqSpi_RecvCmd(uint8_t *cmd_out, uint32_t cmd_len);

/**
 * @brief  取 H7_READY 握手超时次数(累计,F407 启动后开始算)
 * @note   tio 看到这计数一直涨 = H7 没拉高 READY,链路有故障
 */
uint32_t AcqSpi_GetReadyTimeoutCount(void);

#endif /* SPI_H */
