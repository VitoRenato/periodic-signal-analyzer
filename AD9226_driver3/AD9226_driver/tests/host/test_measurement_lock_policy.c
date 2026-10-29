#include "measurement_lock_policy.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
  /* H743 上电后的首个空模型不能让 F407 提前进入 ERROR。 */
  assert(MeasurementLockPolicy_Decide(true, false, true, 204u, 2000u)
         == MEASUREMENT_LOCK_WAIT);

  /* 最终采样率上的有效模型立即冻结显示。 */
  assert(MeasurementLockPolicy_Decide(true, true, true, 322u, 2000u)
         == MEASUREMENT_LOCK_FREEZE);

  /* 高采样率观察阶段的显示结果只作候选，不立即冻结。 */
  assert(MeasurementLockPolicy_Decide(true, true, false, 322u, 2000u)
         == MEASUREMENT_LOCK_WAIT);

  /* 只有超过总响应时限才进入错误状态。 */
  assert(MeasurementLockPolicy_Decide(false, false, false, 2000u, 2000u)
         == MEASUREMENT_LOCK_TIMEOUT);

  puts("measurement_lock_policy_tests: PASS");
  return 0;
}
