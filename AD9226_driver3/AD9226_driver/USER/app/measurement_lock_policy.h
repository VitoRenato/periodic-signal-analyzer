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

/**
 * @param rate_settled 结果来自已稳定的最终采样率（非高采样率观察阶段）。
 */
MeasurementLockDecision MeasurementLockPolicy_Decide(
    bool has_new_model,
    bool model_displayable,
    bool rate_settled,
    uint32_t elapsed_ms,
    uint32_t deadline_ms);

#endif /* MEASUREMENT_LOCK_POLICY_H */
