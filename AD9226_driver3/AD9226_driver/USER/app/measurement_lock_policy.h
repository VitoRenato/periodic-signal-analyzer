#ifndef MEASUREMENT_LOCK_POLICY_H
#define MEASUREMENT_LOCK_POLICY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  MEASUREMENT_LOCK_WAIT = 0,
  MEASUREMENT_LOCK_FREEZE,
  MEASUREMENT_LOCK_TIMEOUT
} MeasurementLockDecision;

MeasurementLockDecision MeasurementLockPolicy_Decide(
    bool has_new_model,
    bool model_displayable,
    uint32_t elapsed_ms,
    uint32_t deadline_ms);

#endif /* MEASUREMENT_LOCK_POLICY_H */
