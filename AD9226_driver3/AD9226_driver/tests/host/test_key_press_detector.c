#include "key_press_detector.h"

#include <assert.h>
#include <stdio.h>

static void test_short_press_and_release_bounce(void)
{
  KeyPressDetector detector;
  uint32_t t = 1000u;

  KeyPressDetector_Init(&detector, false, t);
  assert(KeyPressDetector_Update(&detector, t, false, false, 0u) == KEY_EVENT_NONE);

  /* 按下并去抖，不提前上报。 */
  t += 10u;
  assert(KeyPressDetector_Update(&detector, t, true, true, t) == KEY_EVENT_NONE);
  t += 20u;
  assert(KeyPressDetector_Update(&detector, t, true, false, 0u) == KEY_EVENT_NONE);
  t += 10u;
  assert(KeyPressDetector_Update(&detector, t, true, false, 0u) == KEY_EVENT_NONE);

  /* 正常短按松开：上报一次短按。 */
  t += 80u;
  assert(KeyPressDetector_Update(&detector, t, false, false, 0u) == KEY_EVENT_NONE);
  t += 30u;
  assert(KeyPressDetector_Update(&detector, t, false, false, 0u) == KEY_EVENT_SHORT);

  /* 松开触点抖动造成的 EXTI 下降沿不应再次触发短按。 */
  t += 5u;
  assert(KeyPressDetector_Update(&detector, t, false, true, t) == KEY_EVENT_NONE);
  t += 50u;
  assert(KeyPressDetector_Update(&detector, t, false, false, 0u) == KEY_EVENT_NONE);
}

static void test_long_press_1500ms_does_not_fire_short_on_release(void)
{
  KeyPressDetector detector;
  uint32_t t = 5000u;

  KeyPressDetector_Init(&detector, false, t);
  t += 10u;
  assert(KeyPressDetector_Update(&detector, t, true, true, t) == KEY_EVENT_NONE);
  t += 30u;
  assert(KeyPressDetector_Update(&detector, t, true, false, 0u) == KEY_EVENT_NONE);

  /* 1499 ms 时还未达到 1.5 s 阈值。 */
  assert(KeyPressDetector_Update(&detector, 5010u + 1499u, true, false, 0u) == KEY_EVENT_NONE);
  /* 满 1500 ms 立即上报一次长按。 */
  assert(KeyPressDetector_Update(&detector, 5010u + 1500u, true, false, 0u) == KEY_EVENT_LONG);
  /* 继续按住不重复上报。 */
  assert(KeyPressDetector_Update(&detector, 5010u + 2200u, true, false, 0u) == KEY_EVENT_NONE);

  /* 松开时不再触发短按。 */
  t = 5010u + 2300u;
  assert(KeyPressDetector_Update(&detector, t, false, false, 0u) == KEY_EVENT_NONE);
  assert(KeyPressDetector_Update(&detector, t + 40u, false, false, 0u) == KEY_EVENT_NONE);
}

static void test_missed_tap_during_blocking_work(void)
{
  KeyPressDetector detector;

  KeyPressDetector_Init(&detector, false, 100u);
  assert(KeyPressDetector_Update(&detector, 100u, false, false, 0u) == KEY_EVENT_NONE);

  /* 主循环阻塞期间发生一次按下又松开：轮询时引脚已恢复未按下，但 EXTI 记下了下降沿。 */
  assert(KeyPressDetector_Update(&detector, 500u, false, true, 420u) == KEY_EVENT_SHORT);
  assert(KeyPressDetector_Update(&detector, 550u, false, false, 0u) == KEY_EVENT_NONE);
}

static void test_held_at_boot_requires_release_first(void)
{
  KeyPressDetector detector;

  KeyPressDetector_Init(&detector, true, 0u);
  assert(KeyPressDetector_Update(&detector, 2000u, true, false, 0u) == KEY_EVENT_NONE);
  assert(KeyPressDetector_Update(&detector, 2010u, false, false, 0u) == KEY_EVENT_NONE);
  assert(KeyPressDetector_Update(&detector, 2050u, false, false, 0u) == KEY_EVENT_NONE);
}

int main(void)
{
  test_short_press_and_release_bounce();
  test_long_press_1500ms_does_not_fire_short_on_release();
  test_missed_tap_during_blocking_work();
  test_held_at_boot_requires_release_first();
  puts("key_press_detector_tests: PASS");
  return 0;
}
