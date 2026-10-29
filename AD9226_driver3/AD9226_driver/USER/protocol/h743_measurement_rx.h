#ifndef H743_MEASUREMENT_RX_H
#define H743_MEASUREMENT_RX_H

#include "h743_measurement_model.h"

#include <stdbool.h>
#include <stdint.h>

/* v3 命令槽：22 B，CRC 覆盖控制字段、请求代号、实际采样率与 6 B 载荷。 */
#define H743_MEASUREMENT_FRAME_SIZE 22U

typedef struct
{
    H743MeasurementModel staging_model;
    H743MeasurementModel published_model;
    uint32_t published_sequence;
    uint32_t active_analysis_rate_Hz;
    uint16_t expected_request_generation;
    uint16_t active_request_generation;
    uint8_t staging_active;
    uint8_t published_valid;
    uint8_t active_model_id;
    uint8_t expected_page;
    uint8_t recovered;
    uint8_t seen_pages;
    /* 只有收到本轮会话起始标记后才允许发布模型。 */
    uint8_t session_bound;
    uint8_t session_start_event;
} H743MeasurementRx;

void H743MeasurementRx_Init(H743MeasurementRx *receiver);

/**
 * @brief 接收一页；仅在第五页完成且整组语义合法、且已绑定本轮会话时返回 true。
 */
bool H743MeasurementRx_ProcessFrame(
    H743MeasurementRx *receiver,
    const uint8_t *frame,
    uint32_t length);

bool H743MeasurementRx_GetPublished(
    const H743MeasurementRx *receiver,
    H743MeasurementModel *model);

/** @brief 设置本轮请求代号；代号不匹配的回传页一律丢弃。 */
void H743MeasurementRx_SetRequestGeneration(
    H743MeasurementRx *receiver,
    uint16_t request_generation);

/** @brief 取走一次性“本轮会话已被 H743 接受”事件。 */
bool H743MeasurementRx_TakeSessionStart(H743MeasurementRx *receiver);

#endif
