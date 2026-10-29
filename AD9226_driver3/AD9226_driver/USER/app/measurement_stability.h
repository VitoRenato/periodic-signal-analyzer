#ifndef MEASUREMENT_STABILITY_H
#define MEASUREMENT_STABILITY_H

#include "h743_measurement_model.h"

typedef struct
{
    H743MeasurementModel previous;
    uint32_t last_sequence;
    uint16_t last_generation;
    uint8_t consecutive;
    bool have_sequence;
} MeasurementStability;

void MeasurementStability_Reset(MeasurementStability *state);
bool MeasurementStability_Observe(MeasurementStability *state,
                                  const H743MeasurementModel *model,
                                  bool displayable);

#endif
