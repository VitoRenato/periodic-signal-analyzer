#include "measurement_lock_policy.h"

MeasurementLockDecision MeasurementLockPolicy_Decide(
    bool has_new_model,
    bool model_displayable,
    uint32_t elapsed_ms,
    uint32_t deadline_ms)
{
  if (has_new_model && model_displayable)
  {
    return MEASUREMENT_LOCK_FREEZE;
  }
  if (elapsed_ms >= deadline_ms)
  {
    return MEASUREMENT_LOCK_TIMEOUT;
  }
  return MEASUREMENT_LOCK_WAIT;
}
