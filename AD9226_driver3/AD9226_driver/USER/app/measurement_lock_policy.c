#include "measurement_lock_policy.h"

MeasurementLockDecision MeasurementLockPolicy_Decide(
    bool has_new_model,
    bool model_displayable,
    bool rate_settled,
    uint32_t elapsed_ms,
    uint32_t deadline_ms)
{
  if (elapsed_ms > deadline_ms)
  {
    return MEASUREMENT_LOCK_TIMEOUT;
  }
  if (has_new_model && model_displayable && rate_settled)
  {
    return MEASUREMENT_LOCK_FREEZE;
  }
  if (elapsed_ms >= deadline_ms)
  {
    return MEASUREMENT_LOCK_TIMEOUT;
  }
  return MEASUREMENT_LOCK_WAIT;
}
