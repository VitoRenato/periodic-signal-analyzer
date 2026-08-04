/**
 * @file    spicmd.h
 * @brief   解析 SPI1 全双工帧中命令槽的数据(16 字节),用于动态改采样率
 * @note    命令帧格式(小端):
 *            [0-1]  magic     u16  固定 0xA55A
 *            [2]    type      u8   0x00=NOP(空槽)
 *                                0x01=SET_FS
 *                                0x02=SET_EQ_F0(等效采样:给出基频 Hz,F407 自己选 divider)
 *            [3]    flags     u8   bit0=测量值有效
 *            [4-7]  fs_hz     u32  SET_FS=目标采样率(Hz);SET_EQ_F0=基频 f0(Hz)
 *            [8-9]  crc16     u16  CRC16/CCITT-FALSE (poly 0x1021 init 0xFFFF) 覆盖 0~7
 *            [10-13]fundamental_hz u32  H7 测得的基频(Hz)，flags bit0=1 时有效
 *            [14-15]vpp_mv    u16  H7 测得的 Vpp(mV)，flags bit0=1 时有效
 *
 *          命令由 H723 从机在 SPI1 MISO 命令槽中发回,F407 主机在 spi.c 中收满后
 *          调用 SpiCmd_ProcessRx() 解析。
 */
#ifndef SPICMD_H
#define SPICMD_H

#include <stdbool.h>
#include <stdint.h>
#include "h743_measurement_model.h"

/** @brief  初始化命令解析模块的计数器 */
void SpiCmd_Init(void);

/**
 * @brief  解析从 SPI1 命令槽收到的原始字节
 * @param  rx_buf : 命令槽缓冲指针(长度至少 len)
 * @param  len    : 缓冲长度,必须 ≥ 10
 * @note   空槽(magic=0)会被忽略,不计入错误
 */
void SpiCmd_ProcessRx(const uint8_t *rx_buf, uint32_t len);

/** @brief  已成功应用的命令数(调试用) */
uint32_t SpiCmd_GetOkCount(void);
/** @brief  帧错误数(magic/CRC 错,不含空槽) */
uint32_t SpiCmd_GetErrCount(void);

/**
 * @brief  取 H7 最近一次有效测量值
 * @param  freq_hz : 输出基频(Hz)
 * @param  vpp_mv  : 输出 Vpp(mV)
 * @return true=本次命令槽中 flags bit0 置位,数据有效
 */
bool SpiCmd_GetMeasurement(uint32_t *freq_hz, uint16_t *vpp_mv);

/** @brief 原子复制最近一次完整收到的五页测量模型。 */
bool SpiCmd_GetMeasurementModel(H743MeasurementModel *model);

#endif /* SPICMD_H */
