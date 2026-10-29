/**
 * @file    spi.c
 * @brief   F407 主机通过 SPI1 与 H723 从机全双工通信(单 SPI 总线)
 * @note    SPI1 配 8 bit / 5.25 MHz / Mode 0 / MSB first
 *          H723 SPI 从机需要配成同 Mode, 同样 baud rate
 *
 *  一帧传输 = 20 B 帧头 + 4096 点样本(8192 B) = 8212 B
 *  - F407 MOSI: [20 B 帧头(见 acq_frame_tx.h)][8192 B 样本]
 *  - H743 MISO: [20 B 命令响应/测量页][8192 B 占位]
 * 利用 SPI 全双工特性,同时完成"发数据到 H7"和"从 H7 收命令"。
 *
 *  §3.1 SPI_A_READY 硬件握手:
 *  - F407 端 PA4 输入,等 H7 拉高才开始发
 *  - 内部上拉,H7 没接时 PA4=1,自动跳过握手(兼容自环/冒烟)
 *  - 500ms 超时:等不到 H7_READY,丢本帧 + 错误计数+1,F407 不卡死
 *
 *  数据格式:与技术手册 §4 一致,每样本 16 bit,仅低 12 bit 有效,高 4 bit = 0。
 *  发送前会屏蔽 buf[i] 高 4 位,避免 GPIOF->IDR 的 PF12~PF15 杂散电平污染 H7。
 */
#include "spi.h"
#include "main.h"      /* hspi1, huart1, __io_putchar (printf) */
#include "ad9226.h"    /* AD9226_SAMPLE_COUNT */
#include "acq_frame_tx.h"
#include <stdio.h>
#include <string.h>

extern SPI_HandleTypeDef hspi1;

/* H7_READY 握手超时累计,只增不减,供 tio/调试查询 */
static volatile uint32_t s_ready_timeout_count = 0;

/* SPI 阻塞传输超时: 8208 B @ 5.25 MHz 理论 ~12.5 ms,留 40 倍裕量 */
#define ACQ_SPI_XFER_TIMEOUT_MS   500U

/* SPI1 全双工帧缓冲,放 bss 避免 8KB+ 栈溢出。 */
#define ACQ_SPI_SAMPLE_BYTES    (AD9226_SAMPLE_COUNT * 2U)
#define ACQ_SPI_FRAME_BYTES     (ACQ_SPI_CMD_SLOT_BYTES + ACQ_SPI_SAMPLE_BYTES)

static uint8_t s_spi1_tx_buf[ACQ_SPI_FRAME_BYTES];
static uint8_t s_spi1_rx_buf[ACQ_SPI_FRAME_BYTES];

static HAL_StatusTypeDef AcqSpi_WaitReady(uint32_t timeout_ms)
{
    const uint32_t t_ready = HAL_GetTick();
    while (HAL_GPIO_ReadPin(ACQ_SPI_READY_PORT, ACQ_SPI_READY_PIN) == GPIO_PIN_RESET) {
        if ((HAL_GetTick() - t_ready) >= timeout_ms) {
            s_ready_timeout_count++;
            /* 降频打印，避免 H7 处理慢时刷屏；每 10 次或首次才输出 */
            if (s_ready_timeout_count == 1U
                || (s_ready_timeout_count % 10U) == 0U) {
                printf("[SPI] H7_READY timeout, err=%lu\r\n",
                       (unsigned long)s_ready_timeout_count);
            }
            return HAL_TIMEOUT;
        }
    }
    return HAL_OK;
}

uint32_t AcqSpi_GetReadyTimeoutCount(void)
{
    return s_ready_timeout_count;
}

HAL_StatusTypeDef AcqSpi_SendSamples(const uint16_t *buf, uint32_t n,
                                     const AcqFrameTxInfo *info)
{
    if (buf == NULL || n == 0 || info == NULL) {
        return HAL_ERROR;
    }
    if (n > AD9226_SAMPLE_COUNT) {
        return HAL_ERROR;
    }

    /* 等 H7 准备好接收样本 */
    HAL_StatusTypeDef rst = AcqSpi_WaitReady(ACQ_SPI_READY_TIMEOUT_MS);
    if (rst != HAL_OK) {
        return rst;
    }

    /* 屏蔽高 4 位,保证 SPI 数据与技术手册一致:bit 15~12 = 0 */
    memset(&s_spi1_tx_buf[ACQ_SPI_CMD_SLOT_BYTES], 0, ACQ_SPI_SAMPLE_BYTES);
    for (uint32_t i = 0; i < n; i++) {
        uint16_t sample = buf[i] & 0x0FFFu;
        s_spi1_tx_buf[ACQ_SPI_CMD_SLOT_BYTES + i * 2U + 0] = (uint8_t)(sample & 0xFFu);
        s_spi1_tx_buf[ACQ_SPI_CMD_SLOT_BYTES + i * 2U + 1] = (uint8_t)((sample >> 8) & 0xFFu);
    }

    /* 前 20 B 为帧头:采样分频、帧序号和样本 CRC,H743 据此校验对齐并取 Fs */
    if (!AcqFrameTx_Encode(info,
                           &s_spi1_tx_buf[ACQ_SPI_CMD_SLOT_BYTES],
                           ACQ_SPI_SAMPLE_BYTES,
                           s_spi1_tx_buf)) {
        return HAL_ERROR;
    }

    /* SPI1 全双工:同时发送 8212 B 样本并接收 8212 B(含命令槽) */
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi1,
                                                    s_spi1_tx_buf,
                                                    s_spi1_rx_buf,
                                                    ACQ_SPI_FRAME_BYTES,
                                                    ACQ_SPI_XFER_TIMEOUT_MS);
    if (st != HAL_OK) {
        printf("[SPI1] full-duplex failed st=%d err=0x%lX\r\n",
               (int)st, (unsigned long)hspi1.ErrorCode);
    }
    return st;
}

HAL_StatusTypeDef AcqSpi_RecvCmd(uint8_t *cmd_out, uint32_t cmd_len)
{
    if (cmd_out == NULL || cmd_len == 0) {
        return HAL_ERROR;
    }
    if (cmd_len > ACQ_SPI_CMD_SLOT_BYTES) {
        cmd_len = ACQ_SPI_CMD_SLOT_BYTES;
    }

    /* 命令槽在接收缓冲最前面 16 B */
    memcpy(cmd_out, s_spi1_rx_buf, cmd_len);
    return HAL_OK;
}

/**
 * @brief  自环测试:PA7(MOSI)短接 PA6(MISO),验证 SPI 配置和数据完整性
 * @param  tx_buf : 发送缓冲(来自 AD9226_GetBuf)
 * @param  n      : 样本数(4096)
 * @return HAL_OK = 数据完全一致, HAL_ERROR = 有 mismatch
 * @note   测试前必须用杜邦线把 PA7 <-> PA6 短接
 */
HAL_StatusTypeDef AcqSpi_LoopbackTest(const uint16_t *tx_buf, uint32_t n)
{
    const uint32_t total = n * 2U;

    /* 简单自环:直接发送样本并读回,不携带命令槽 */
    static uint8_t rx_buf[8192];   /* 4096 * 2, 放 bss */
    const uint32_t t0 = HAL_GetTick();
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi1,
                                                    (const uint8_t *)tx_buf,
                                                    rx_buf,
                                                    total,
                                                    HAL_MAX_DELAY);
    const uint32_t dt_ms = HAL_GetTick() - t0;

    if (st != HAL_OK) {
        printf("[LB] SPI failed st=%d\r\n", (int)st);
        return st;
    }

    /* 逐字节比对 */
    uint32_t mism = 0;
    uint32_t first_mism = 0xFFFFFFFFU;
    const uint8_t *tx = (const uint8_t *)tx_buf;
    for (uint32_t i = 0; i < total; i++) {
        if (tx[i] != rx_buf[i]) {
            if (mism == 0) first_mism = i;
            mism++;
            if (mism >= 8) break;   /* 前 8 个 mismatch 够诊断了 */
        }
    }

    printf("[LB] %lu B in %lu ms, mism=%lu",
           (unsigned long)total, (unsigned long)dt_ms, (unsigned long)mism);
    if (mism == 0) {
        printf(" PERFECT\r\n");
        return HAL_OK;
    }
    printf(" first@%lu: tx=0x%02X rx=0x%02X\r\n",
           (unsigned long)first_mism,
           tx[first_mism], rx_buf[first_mism]);
    return HAL_ERROR;
}
