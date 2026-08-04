#ifndef H743_MEASUREMENT_MODEL_H
#define H743_MEASUREMENT_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#define H743_MEASUREMENT_MAX_COMPONENTS 3U

typedef struct
{
    uint8_t order;
    uint32_t amplitude_peak_uV;
    int16_t phase_q15;
    uint8_t valid;
} H743MeasurementComponent;

typedef struct
{
    uint32_t fundamental_Hz;
    uint32_t upp_uV;
    uint32_t urms_uV;
    uint8_t component_count;
    uint8_t status;
    uint8_t quality_flags;
    uint8_t recovered;
    uint8_t model_id;
    uint32_t published_sequence;
    H743MeasurementComponent component[H743_MEASUREMENT_MAX_COMPONENTS];
} H743MeasurementModel;

/** @brief 判断完整模型是否满足正式显示的状态和三分量语义。 */
bool H743MeasurementModel_IsDisplayable(const H743MeasurementModel *model);

#endif
