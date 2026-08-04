#include "measurement_lock_policy.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
  /* H743 上电后的首个空模型不能让 F407 提前进入 ERROR。 */
  assert(MeasurementLockPolicy_Decide(true, false, 204u, 2000u)
         == MEASUREMENT_LOCK_WAIT);

  /* 后续有效模型应立即冻结显示。 */
  assert(MeasurementLockPolicy_Decide(true, true, 322u, 2000u)
         == MEASUREMENT_LOCK_FREEZE);

  /* 只有超过总响应时限才进入错误状态。 */
  assert(MeasurementLockPolicy_Decide(false, false, 2000u, 2000u)
         == MEASUREMENT_LOCK_TIMEOUT);

  puts("measurement_lock_policy_tests: PASS");
  return 0;
}
