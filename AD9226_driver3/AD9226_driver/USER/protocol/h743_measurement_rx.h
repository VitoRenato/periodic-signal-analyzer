#ifndef H743_MEASUREMENT_RX_H
#define H743_MEASUREMENT_RX_H

#include "h743_measurement_model.h"

#include <stdbool.h>
#include <stdint.h>

#define H743_MEASUREMENT_FRAME_SIZE 16U

typedef struct
{
    H743MeasurementModel staging_model;
    H743MeasurementModel published_model;
    uint8_t staging_active;
    uint8_t published_valid;
    uint8_t active_model_id;
    uint8_t expected_page;
    uint8_t recovered;
    uint8_t seen_pages;
    uint32_t published_sequence;
} H743MeasurementRx;

void H743MeasurementRx_Init(H743MeasurementRx *receiver);

/**
 * @brief 接收一页；仅在第五页完成且整组语义合法时返回 true。
 */
bool H743MeasurementRx_ProcessFrame(
    H743MeasurementRx *receiver,
    const uint8_t *frame,
    uint32_t length);

bool H743MeasurementRx_GetPublished(
    const H743MeasurementRx *receiver,
    H743MeasurementModel *model);

#endif
