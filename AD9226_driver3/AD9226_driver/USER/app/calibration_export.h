/**
 * @file calibration_export.h
 * @brief 'c' 原码导出的隔离状态机：只请求一帧 ADC 原始码，不改变测量会话、
 *        屏幕内容或自动连续测量设置。
 */
#ifndef CALIBRATION_EXPORT_H
#define CALIBRATION_EXPORT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  CAL_EXPORT_NONE = 0,
  CAL_EXPORT_START_CAPTURE, /* 当前没有进行中的采集，调用方需要启动一帧 */
  CAL_EXPORT_EMIT           /* 已拿到一帧，调用方导出原始码 */
} CalExportAction;

typedef struct
{
  uint8_t capturing;
} CalibrationExport;

void CalibrationExport_Init(CalibrationExport *export_state);

/**
 * @brief 处理 'c' 请求。
 * @param acquiring 当前测量会话是否正在采集（是则等待该帧完成，不重启 ADC）。
 */
CalExportAction CalibrationExport_Request(
    CalibrationExport *export_state,
    bool acquiring);

/** @brief 一帧 ADC 完成后调用；若本次请求正在等待，则返回 EMIT。 */
CalExportAction CalibrationExport_OnFrameDone(CalibrationExport *export_state);

bool CalibrationExport_IsCapturing(const CalibrationExport *export_state);

#endif /* CALIBRATION_EXPORT_H */
